#pragma once
#include <stdint.h>

// Pure quorum logic behind TimeSyncHelper: no Arduino, RTC or GPS types, so
// the decision rules can be unit-tested on the host.
//
// Samples are stored as an OFFSET (sender_time - local_time_at_receipt) plus
// the local receipt time, not as an absolute timestamp. Two honest peers with
// the same wall clock therefore produce the same offset no matter how far
// apart in time they were heard, so a quiet mesh (adverts hours apart) can
// still find agreement. The local clock only has to tick monotonically; it is
// never compared with the sender's clock directly.
namespace timesync {

static const int BUF_SIZE = 10;

// sender = 0 means "unknown": such samples are never merged with each other.
struct Sample {
  int64_t  offset;   // sender_time - local_time_at_receipt (seconds)
  uint32_t local;    // local monotonic receipt time (seconds)
  uint32_t sender;   // 0 = unknown
};

// Appends a sample, oldest first. A known sender keeps only its latest sample,
// so one chatty node (or one room server replaying its history) is one vote,
// not a quorum on its own.
inline void push(Sample* buf, int& count, int64_t offset, uint32_t local, uint32_t sender) {
  if (sender != 0) {
    for (int i = 0; i < count; i++) {
      if (buf[i].sender != sender) continue;
      for (int j = i; j < count - 1; j++) buf[j] = buf[j + 1];
      count--;
      break;
    }
  }
  if (count == BUF_SIZE) {
    for (int j = 0; j < BUF_SIZE - 1; j++) buf[j] = buf[j + 1];
    count--;
  }
  buf[count++] = { offset, local, sender };
}

// Channel messages carry no sender identity, only "<name>: <text>". Hashing
// the name is not authentication, but it stops a single sender from filling
// the buffer. Returns 0 (unknown) when there is no "name:" prefix.
inline uint32_t senderFromChannelText(const char* text) {
  uint32_t h = 2166136261u;  // FNV-1a
  int n = 0;
  for (const char* p = text; *p; p++, n++) {
    if (*p == ':') {
      if (n == 0) return 0;
      return h ? h : 1;
    }
    if (n >= 32) return 0;
    h = (h ^ (uint8_t)*p) * 16777619u;
  }
  return 0;
}

struct Params {
  uint32_t cluster_window;   // offsets within this many seconds agree
  uint32_t drift_threshold;  // smaller differences are left alone
  uint32_t max_jump;         // larger forward steps are rejected as bogus
  uint32_t max_back;         // largest backward step taken automatically
  uint32_t max_age;          // samples older than this many seconds are ignored
  int      quiet_quorum;     // distinct senders enough for a sparse (quiet) mesh
};

enum class Decision : uint8_t { NONE, APPLY, REFUSED_BACK };

struct Result {
  Decision decision;
  uint32_t median;   // time to apply, projected to now (now_local + median offset)
  int      cluster;  // samples in the densest cluster
  int      active;   // non-expired samples considered
};

// A sample is expired once the local clock has moved past max_age since it was
// received. Unsigned subtraction keeps this correct across the local clock's
// wrap as long as max_age is far below 2^32 seconds.
inline bool expired(const Sample& s, uint32_t now_local, uint32_t max_age) {
  return (uint32_t)(now_local - s.local) > max_age;
}

inline int activeCount(const Sample* buf, int count, uint32_t now_local, uint32_t max_age) {
  int n = 0;
  for (int i = 0; i < count; i++) {
    if (!expired(buf[i], now_local, max_age)) n++;
  }
  return n;
}

// unset: the clock has never been trusted, so the first quorum is taken in any
// direction. Otherwise the clock moves forward by up to max_jump, but back only
// by max_back: a backward step makes this node reuse timestamps it already
// sent, which peers treat as replays (adverts after a reboot, logins, ACL
// commands). A small limit still lets a fast-running RC oscillator be pulled
// back.
//
// Quorum rules:
//   trusted: 7 distinct senders whose offsets agree.
//   unset:   3 agreeing senders when the aged window holds three or more
//            distinct peers (the busy-mesh rule inherited from SEC); if it
//            holds no more peers than quiet_quorum, all of those agreeing is
//            enough. The aged window is what lets a sparse mesh accumulate its
//            one or two neighbours over hours, while the "no more peers than
//            the low quorum" test stops a pair of adverts from silently
//            downgrading the three-sender rule in a busier mesh.
inline Result evaluate(const Sample* buf, int count, uint32_t current, uint32_t now_local,
                       bool unset, const Params& p) {
  Result r = { Decision::NONE, 0, 0, 0 };

  // Work on the non-expired samples only.
  int64_t off[BUF_SIZE];
  int n = 0;
  for (int i = 0; i < count; i++) {
    if (expired(buf[i], now_local, p.max_age)) continue;
    off[n++] = buf[i].offset;
  }
  r.active = n;
  if (n == 0) return r;

  // insertion sort by offset (n is small)
  for (int i = 1; i < n; i++) {
    int64_t ko = off[i];
    int j = i - 1;
    while (j >= 0 && off[j] > ko) { off[j + 1] = off[j]; j--; }
    off[j + 1] = ko;
  }

  // densest cluster of agreeing offsets
  int best_count = 0, best_start = 0;
  for (int i = 0; i < n; i++) {
    int cnt = 0;
    while (i + cnt < n && off[i + cnt] - off[i] <= (int64_t)p.cluster_window) cnt++;
    if (cnt > best_count) { best_count = cnt; best_start = i; }
  }
  r.cluster = best_count;

  bool enough = false;
  if (unset) {
    if (best_count >= 3) {
      enough = true;                       // busy: three peers agree
    } else if (n <= p.quiet_quorum && best_count >= p.quiet_quorum) {
      enough = true;                       // quiet: only these peers exist
    }
  } else if (best_count >= 7) {
    enough = true;
  }
  if (!enough) return r;

  r.median = (uint32_t)((int64_t)now_local + off[best_start + best_count / 2]);

  if (unset) {
    r.decision = Decision::APPLY;
  } else if (r.median > current) {
    uint32_t diff = r.median - current;
    if (diff > p.drift_threshold && diff < p.max_jump) r.decision = Decision::APPLY;
  } else {
    uint32_t diff = current - r.median;
    if (diff > p.drift_threshold) {
      r.decision = diff <= p.max_back ? Decision::APPLY : Decision::REFUSED_BACK;
    }
  }
  return r;
}

}  // namespace timesync
