#pragma once
#include <stdint.h>

// Pure quorum logic behind TimeSyncHelper: no Arduino, RTC or GPS types, so
// the decision rules can be unit-tested on the host.
namespace timesync {

static const int BUF_SIZE = 10;

// sender = 0 means "unknown": such samples are never merged with each other.
struct Sample { uint32_t ts; uint32_t sender; };

// Appends a sample, oldest first. A known sender keeps only its latest sample,
// so one chatty node (or one room server replaying its history) is one vote,
// not a quorum on its own.
inline void push(Sample* buf, int& count, uint32_t ts, uint32_t sender) {
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
  buf[count++] = { ts, sender };
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
  uint32_t cluster_window;   // samples within this many seconds agree
  uint32_t drift_threshold;  // smaller differences are left alone
  uint32_t max_jump;         // larger forward steps are rejected as bogus
  uint32_t max_back;         // largest backward step taken automatically
};

enum class Decision : uint8_t { NONE, APPLY, REFUSED_BACK };

struct Result {
  Decision decision;
  uint32_t median;
  int      cluster;  // samples in the densest cluster
};

// unset: the clock has never been trusted, so any quorum value is taken.
// Otherwise the clock moves forward by up to max_jump, but back only by
// max_back: a backward step makes this node reuse timestamps it already sent,
// which peers treat as replays (adverts after a reboot, logins, ACL commands).
// A small limit still lets a fast-running RC oscillator be pulled back.
inline Result evaluate(const Sample* buf, int count, uint32_t current, bool unset, const Params& p) {
  Result r = { Decision::NONE, 0, 0 };
  int n      = unset ? (count < 5 ? count : 5) : count;
  int quorum = unset ? 3 : 7;
  if (n < quorum) return r;

  uint32_t tmp[BUF_SIZE];
  for (int i = 0; i < n; i++) tmp[i] = buf[count - 1 - i].ts;  // newest n
  for (int i = 1; i < n; i++) {
    uint32_t key = tmp[i];
    int j = i - 1;
    while (j >= 0 && tmp[j] > key) { tmp[j + 1] = tmp[j]; j--; }
    tmp[j + 1] = key;
  }

  int best_count = 0, best_start = 0;
  for (int i = 0; i < n; i++) {
    int cnt = 0;
    for (int j = i; j < n && tmp[j] - tmp[i] <= p.cluster_window; j++) cnt++;
    if (cnt > best_count) { best_count = cnt; best_start = i; }
  }
  r.cluster = best_count;
  if (best_count < quorum) return r;

  r.median = tmp[best_start + best_count / 2];
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
