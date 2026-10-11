#include "OfflineRecordRing.h"

#include <string.h>

namespace offline_ring {

static const uint8_t RECORD_MAGIC[4] = { 'O', 'Q', 'R', '1' };
static const uint8_t INDEX_MAGIC[4] = { 'O', 'Q', 'I', '1' };
static constexpr size_t CRC_OFF = 17;   // the CRC covers the record with these 4 bytes zeroed

static void put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static uint16_t get16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t get32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint32_t crc32(const uint8_t* data, size_t len, uint32_t crc) {
  crc = ~crc;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

// The CRC of a record whose CRC field reads as zero.
static uint32_t recordCrc(const uint8_t* rec, size_t len) {
  static const uint8_t zero[4] = { 0, 0, 0, 0 };
  uint32_t crc = crc32(rec, CRC_OFF);
  crc = crc32(zero, 4, crc);
  return crc32(rec + CRC_OFF + 4, len - CRC_OFF - 4, crc);
}

size_t encodeRecord(uint8_t* out, size_t cap, const Record& r,
                    const uint8_t* frame, const uint8_t* route) {
  const size_t total = RECORD_HEADER + r.frame_len + r.route_len;
  if (!out || total > cap || r.frame_len == 0 || !frame) return 0;
  if (r.route_len > 0 && !route) return 0;
  memcpy(out, RECORD_MAGIC, 4);
  put32(&out[4], r.seq);
  put32(&out[8], r.at_s);
  out[12] = r.flags;
  put16(&out[13], r.frame_len);
  put16(&out[15], r.route_len);
  put32(&out[CRC_OFF], 0);
  memcpy(&out[RECORD_HEADER], frame, r.frame_len);
  if (r.route_len > 0) memcpy(&out[RECORD_HEADER + r.frame_len], route, r.route_len);
  put32(&out[CRC_OFF], recordCrc(out, total));
  return total;
}

// Header fields only, checked against the limits; the CRC needs the whole record.
static bool decodeHeader(const uint8_t* in, size_t len, size_t max_frame, size_t max_route, Record& r) {
  if (!in || len < RECORD_HEADER || memcmp(in, RECORD_MAGIC, 4) != 0) return false;
  r.seq = get32(&in[4]);
  r.at_s = get32(&in[8]);
  r.flags = in[12];
  r.frame_len = get16(&in[13]);
  r.route_len = get16(&in[15]);
  // Lengths come from storage, so they are never trusted.
  return r.seq != NO_SEQ && r.frame_len > 0 && r.frame_len <= max_frame && r.route_len <= max_route;
}

bool decodeRecord(const uint8_t* in, size_t len, size_t max_frame, size_t max_route, Record& r) {
  if (!decodeHeader(in, len, max_frame, max_route, r)) return false;
  if (len != RECORD_HEADER + r.frame_len + r.route_len) return false;
  return get32(&in[CRC_OFF]) == recordCrc(in, len);
}

Ring::Ring(Storage& storage, uint16_t slots, uint32_t* slot_seq, uint8_t* scratch,
           size_t scratch_cap, uint16_t max_frame, uint16_t max_route)
    : _storage(storage), _slots(slots), _slot_seq(slot_seq), _scratch(scratch),
      _scratch_cap(scratch_cap), _max_frame(max_frame), _max_route(max_route) {
  for (uint16_t s = 0; s < _slots; s++) _slot_seq[s] = NO_SEQ;
}

bool Ring::open() {
  _usable = false;
  _dirty = false;
  _errors = 0;
  for (uint16_t s = 0; s < _slots; s++) _slot_seq[s] = NO_SEQ;
  if (_slots == 0 || _scratch_cap < RECORD_HEADER + _max_frame + _max_route) return false;

  uint8_t index[INDEX_BYTES];
  if (_storage.readIndex(index, sizeof(index)) != (int)sizeof(index)) return false;
  if (memcmp(index, INDEX_MAGIC, 4) != 0 || get16(&index[4]) != _slots) return false;
  uint32_t acked = get32(&index[8]);
  uint32_t next = get32(&index[12]);

  // The index is written lazily, so the records themselves are the authority on
  // how far the sequence got.
  for (uint16_t s = 0; s < _slots; s++) {
    const int n = _storage.readSlot(s, _scratch, RECORD_HEADER);
    Record r;
    if (n != (int)RECORD_HEADER || !decodeHeader(_scratch, (size_t)n, _max_frame, _max_route, r)) continue;
    if (r.seq % _slots != s) continue;   // not where this seq belongs: not ours
    _slot_seq[s] = r.seq;
    if (r.seq >= next) next = r.seq + 1;
  }
  if (next > _next) _next = next;
  // An index that claims more was consumed than was ever written is damaged:
  // replay everything held rather than nothing.
  _acked = acked <= _next ? acked : 0;
  _usable = true;
  return true;
}

bool Ring::create() {
  _usable = false;
  if (_slots == 0 || _scratch_cap < RECORD_HEADER + _max_frame + _max_route) return false;
  for (uint16_t s = 0; s < _slots; s++) {
    _storage.removeSlot(s);
    _slot_seq[s] = NO_SEQ;
  }
  _acked = _next;   // a fresh ring holds nothing to replay
  _errors = 0;
  if (!writeIndexNow()) return false;
  _usable = true;
  return true;
}

void Ring::failed() {
  if (++_errors >= ERROR_LIMIT) {
    _usable = false;
    _dirty = false;
  }
}

bool Ring::put(const Record& r, const uint8_t* frame, const uint8_t* route) {
  if (!_usable || r.seq == NO_SEQ || r.frame_len > _max_frame || r.route_len > _max_route) return false;
  const uint16_t slot = (uint16_t)(r.seq % _slots);
  const size_t len = encodeRecord(_scratch, _scratch_cap, r, frame, route);
  if (len == 0) return false;
  const uint32_t crc = get32(&_scratch[CRC_OFF]);
  _slot_seq[slot] = NO_SEQ;   // whatever the slot held is gone from here on
  bool ok = _storage.writeSlot(slot, _scratch, len);
  if (ok) {
    // Read back through the storage: a write that reports success but never
    // committed (a full filesystem, a torn close) has to show up here, not at
    // the next boot.
    const int n = _storage.readSlot(slot, _scratch, _scratch_cap);
    Record back;
    ok = n == (int)len && decodeRecord(_scratch, (size_t)n, _max_frame, _max_route, back) &&
         back.seq == r.seq && get32(&_scratch[CRC_OFF]) == crc;
  }
  if (!ok) {
    _storage.removeSlot(slot);
    failed();
    return false;
  }
  _slot_seq[slot] = r.seq;
  if (r.seq >= _next) _next = r.seq + 1;
  _errors = 0;
  return true;
}

bool Ring::get(uint32_t seq, Record& r, uint8_t* frame, uint8_t* route) {
  if (!_usable || seq == NO_SEQ) return false;
  const uint16_t slot = (uint16_t)(seq % _slots);
  if (_slot_seq[slot] != seq) return false;
  const int n = _storage.readSlot(slot, _scratch, _scratch_cap);
  if (n <= 0 || !decodeRecord(_scratch, (size_t)n, _max_frame, _max_route, r) || r.seq != seq) {
    return false;
  }
  if (frame) memcpy(frame, &_scratch[RECORD_HEADER], r.frame_len);
  if (route && r.route_len > 0) memcpy(route, &_scratch[RECORD_HEADER + r.frame_len], r.route_len);
  return true;
}

int Ring::pending(uint32_t* out, int max) const {
  int n = 0;
  if (!_usable || !out || max <= 0) return 0;
  for (uint16_t s = 0; s < _slots; s++) {
    const uint32_t seq = _slot_seq[s];
    if (seq == NO_SEQ || seq < _acked || seq >= _next) continue;
    // Insertion sort, oldest first: the ring is a few dozen slots at most. When
    // `out` is full, the oldest records win and the newest is dropped.
    int i;
    if (n < max) {
      i = n++;
    } else if (seq < out[max - 1]) {
      i = max - 1;
    } else {
      continue;
    }
    while (i > 0 && out[i - 1] > seq) { out[i] = out[i - 1]; i--; }
    out[i] = seq;
  }
  return n;
}

void Ring::setAcked(uint32_t seq) {
  if (seq > _next) seq = _next;
  if (seq <= _acked) return;
  _acked = seq;
  _dirty = true;
}

bool Ring::writeIndexNow() {
  uint8_t index[INDEX_BYTES];
  memcpy(index, INDEX_MAGIC, 4);
  put16(&index[4], _slots);
  put16(&index[6], 0);
  put32(&index[8], _acked);
  put32(&index[12], _next);
  if (!_storage.writeIndex(index, sizeof(index))) return false;
  uint8_t back[INDEX_BYTES];
  return _storage.readIndex(back, sizeof(back)) == (int)sizeof(back) &&
         memcmp(back, index, sizeof(index)) == 0;
}

bool Ring::flushIndex() {
  if (!_usable || !_dirty) return true;
  if (!writeIndexNow()) {
    failed();
    return false;
  }
  _dirty = false;
  _errors = 0;
  return true;
}

}  // namespace offline_ring
