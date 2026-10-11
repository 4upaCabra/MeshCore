#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <Arduino.h>
#include <Mesh.h>
#include <helpers/SensorManager.h>
#include <helpers/TimeSyncQuorum.h>
#include <RTClib.h>

#define TS_MIN_VALID       1577836800u  // 2020-01-01 UTC
#define TS_MAX_VALID       2524608000u  // 2050-01-01 UTC
#define TS_CLUSTER_WINDOW  60u
#define TS_DRIFT_THRESHOLD 120u
#define TS_MAX_JUMP        36000u
#define TS_MAX_BACK        300u
#define TS_MAX_AGE         43200u       // 12 h: covers a peer adverting a few
                                        // times a day while bounding staleness
                                        // to half a day (OS used 24 h)
#define TS_QUIET_QUORUM    2            // peers enough when the aged window
                                        // holds no more than this many

enum class TsSyncResult : uint8_t { NONE, CLOCK, GPS };

class TimeSyncHelper {
public:
  timesync::Sample _buf[timesync::BUF_SIZE] = {};  // oldest first, one per known sender
  int      _buf_count = 0;
  uint32_t _sync_count = 0;
  uint32_t _advert_count = 0;
  uint32_t _valid_count = 0;
  uint32_t _last_sync = 0;
  int32_t  _last_adj = 0;
  int      _best_cluster = 0;
  uint32_t _refused_back = 0;   // last backward step refused (seconds), 0 = none

  // OS persistence hook: true while the clock only came back from storage
  // (RTC memory checkpoint or the /clock file) and no quorum or manual set has
  // confirmed it yet. trySync then treats the clock as "unset": the first
  // quorum may step it in either direction and is not blocked by max_jump,
  // which matters after a long power-off. The caller clears it once the value
  // is confirmed by a quorum or a manual set.
  bool clock_restored = false;

  // Runtime-tunable parameters (reset to defaults on reboot)
  uint32_t cluster_window  = TS_CLUSTER_WINDOW;
  uint32_t drift_threshold = TS_DRIFT_THRESHOLD;
  uint32_t max_jump        = TS_MAX_JUMP;
  uint32_t max_back        = TS_MAX_BACK;
  uint32_t max_age         = TS_MAX_AGE;
  int      quiet_quorum    = TS_QUIET_QUORUM;

  // Local monotonic time in seconds, used only to age samples and to project
  // an agreed offset back onto the local clock. It wraps with millis() after
  // ~49.7 days, at which point the oldest samples simply expire.
  uint32_t localNow() const { return (uint32_t)(millis() / 1000u); }

  // Feed advert timestamp (increments _advert_count).
  // pub_hash: first 4 bytes of the sender's public key.
  // Returns true if added to buffer.
  bool feedAdvert(uint32_t ts, uint32_t pub_hash);

  // Feed message timestamp (no advert counting, MAX_JUMP guard after first sync).
  // cur_time: current RTC time for the jump check.
  // sender: same key space as feedAdvert's pub_hash, or
  // timesync::senderFromChannelText() for channel messages; 0 if unknown.
  bool feedMessage(uint32_t ts, uint32_t cur_time, uint32_t sender);

  // Run quorum algorithm and apply sync if quorum reached.
  // app_time_locked: caller passes true during CMD_SET_DEVICE_TIME holdoff.
  // Returns NONE/CLOCK/GPS.
  TsSyncResult trySync(mesh::RTCClock* rtc, SensorManager* sensors, bool app_time_locked = false);

  // Reset sync state, preserve runtime params.
  void reset() {
    memset(_buf, 0, sizeof(_buf));
    _buf_count = _sync_count = _advert_count = _valid_count = 0;
    _last_sync = 0; _last_adj = 0; _best_cluster = 0; _refused_back = 0;
  }

  // Build timesync status reply into buf (truncated to size).
  void buildReply(char* buf, size_t size, mesh::RTCClock* rtc) const;

  static uint32_t pubHash(const uint8_t* pub_key) {
    uint32_t h;
    memcpy(&h, pub_key, 4);
    return h;
  }
};
