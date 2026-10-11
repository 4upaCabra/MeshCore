#include "TimeSyncHelper.h"
#include <RTClib.h>
#include <helpers/sensors/LocationProvider.h>

bool TimeSyncHelper::feedAdvert(uint32_t ts, uint32_t pub_hash) {
  _advert_count++;
  if (ts <= TS_MIN_VALID || ts >= TS_MAX_VALID) return false;
  _valid_count++;
  timesync::push(_buf, _buf_count, ts, pub_hash);
  return true;
}

bool TimeSyncHelper::feedMessage(uint32_t ts, uint32_t cur_time, uint32_t sender) {
  if (ts <= TS_MIN_VALID || ts >= TS_MAX_VALID) return false;
  if (_sync_count > 0) {
    uint32_t diff = (ts > cur_time) ? ts - cur_time : cur_time - ts;
    if (diff >= max_jump) return false;
  }
  timesync::push(_buf, _buf_count, ts, sender);
  return true;
}

TsSyncResult TimeSyncHelper::trySync(mesh::RTCClock* rtc, SensorManager* sensors, bool app_time_locked) {
  if (app_time_locked) return TsSyncResult::NONE;

  bool unset = (!rtc->isTimeReliable() || clock_restored) && (_sync_count == 0);
  uint32_t current = rtc->getCurrentTime();
  timesync::Params params = { cluster_window, drift_threshold, max_jump, max_back };
  timesync::Result r = timesync::evaluate(_buf, _buf_count, current, unset, params);
  if (r.cluster > _best_cluster) _best_cluster = r.cluster;

  if (r.decision == timesync::Decision::REFUSED_BACK) {
    _refused_back = current - r.median;
    MESH_DEBUG_PRINTLN("TimeSync: refused %lu s backward step (max_back %lu)",
                       (unsigned long)_refused_back, (unsigned long)max_back);
    return TsSyncResult::NONE;
  }
  if (r.decision != timesync::Decision::APPLY) return TsSyncResult::NONE;

  int32_t adj = (int32_t)r.median - (int32_t)current;
  _last_adj  = adj;
  _last_sync = r.median;
  _refused_back = 0;
  _sync_count++;

  // prefer GPS re-sync if available
  if (sensors) {
    LocationProvider* gps = sensors->getLocationProvider();
    if (gps && gps->isEnabled() && gps->isValid()) {
      gps->syncTime();
      MESH_DEBUG_PRINTLN("TimeSync: GPS re-sync (quorum drift %ld s)", (long)adj);
      return TsSyncResult::GPS;
    }
  }
  rtc->setCurrentTime(r.median);
  MESH_DEBUG_PRINTLN("TimeSync: clock adj %ld s (quorum %d/%d)", (long)adj, r.cluster, _buf_count);
  return TsSyncResult::CLOCK;
}

void TimeSyncHelper::buildReply(char* buf, size_t size, mesh::RTCClock* rtc) const {
  if (size == 0) return;
  uint32_t now = rtc->getCurrentTime();
  DateTime dt(now);
  int len;
  if (_sync_count == 0) {
    bool unset_mode = !rtc->isTimeReliable() || clock_restored;
    int n = _buf_count < (unset_mode ? 5 : 10) ? _buf_count : (unset_mode ? 5 : 10);
    len = snprintf(buf, size,
      "TimeSync: no sync yet\nAdverts: %lu rx / %lu valid\nBuf: %d/10 (best cluster: %d/%d need %d)\nClock: %02d:%02d:%02d %d-%02d-%02d UTC",
      (unsigned long)_advert_count, (unsigned long)_valid_count,
      _buf_count, _best_cluster, n, unset_mode ? 3 : 7,
      dt.hour(), dt.minute(), dt.second(), dt.year(), dt.month(), dt.day());
  } else {
    uint32_t ago = now > _last_sync ? now - _last_sync : 0;
    DateTime ls(_last_sync);
    len = snprintf(buf, size,
      "TimeSync: %lu syncs\nLast: %02d:%02d %d-%02d-%02d UTC (%lus ago)\nAdj: %+lds\nAdverts: %lu rx / %lu valid\nBuf: %d/10\nClock: %02d:%02d:%02d %d-%02d-%02d UTC",
      (unsigned long)_sync_count,
      ls.hour(), ls.minute(), ls.year(), ls.month(), ls.day(),
      (unsigned long)ago, (long)_last_adj,
      (unsigned long)_advert_count, (unsigned long)_valid_count,
      _buf_count,
      dt.hour(), dt.minute(), dt.second(), dt.year(), dt.month(), dt.day());
  }
  if (_refused_back && len >= 0 && (size_t)len < size) {
    snprintf(buf + len, size - len, "\nBack: -%lus refused", (unsigned long)_refused_back);
  }
}
