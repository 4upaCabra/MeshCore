#pragma once

#include <stddef.h>
#include <stdint.h>

// A ring of fixed slots that keeps the companion's offline queue across a reboot,
// one record per slot.
//
// A record is identified by its sequence number, never by its position: it lands
// in slot seq % slots and carries its seq, so a slot that a later record has taken
// over is never mistaken for the one asked about. What the app has consumed is a
// single watermark — every seq below it is done — that the caller derives from
// its own queue; there is no counter to drift when the queue drops an entry out
// of order.
//
// Each record holds the queued frame and, optionally, the route record that goes
// with it: [snr x4][rssi][raw packet], the payload of a LogRxData push. A CRC over
// the whole record makes a torn or half-committed write read as "absent" instead
// of as a message.
namespace offline_ring {

static constexpr uint32_t NO_SEQ = 0xFFFFFFFFu;
static constexpr size_t RECORD_HEADER = 21;   // magic, seq, at_s, flags, lengths, crc
static constexpr size_t INDEX_BYTES = 16;
static constexpr uint8_t ERROR_LIMIT = 3;     // consecutive failures before the ring steps aside

struct Record {
  uint32_t seq;
  uint32_t at_s;       // unix seconds when the frame was queued
  uint8_t flags;       // the queue entry's compat flags
  uint16_t frame_len;
  uint16_t route_len;  // 0 = no route kept for this frame
};

// Where the ring keeps its bytes: one blob per slot plus one index blob. A write
// replaces the whole blob. Reads return the byte count, or -1 when there is none.
class Storage {
public:
  virtual ~Storage() {}
  virtual bool writeSlot(uint16_t slot, const uint8_t* data, size_t len) = 0;
  virtual int readSlot(uint16_t slot, uint8_t* buf, size_t cap) = 0;
  virtual void removeSlot(uint16_t slot) = 0;
  virtual bool writeIndex(const uint8_t* data, size_t len) = 0;
  virtual int readIndex(uint8_t* buf, size_t cap) = 0;
};

uint32_t crc32(const uint8_t* data, size_t len, uint32_t crc = 0);

// Encoded size of a record, or 0 when it does not fit `cap`.
size_t encodeRecord(uint8_t* out, size_t cap, const Record& r,
                    const uint8_t* frame, const uint8_t* route);
// Validates a whole record (header, lengths against the limits, CRC). `len` is
// the blob's size; the frame and route start at RECORD_HEADER and right after it.
bool decodeRecord(const uint8_t* in, size_t len, size_t max_frame, size_t max_route, Record& r);

class Ring {
public:
  // slot_seq: a table of `slots` entries owned by the caller. scratch: at least
  // RECORD_HEADER + max_frame + max_route bytes; every read and write goes through it.
  Ring(Storage& storage, uint16_t slots, uint32_t* slot_seq, uint8_t* scratch,
       size_t scratch_cap, uint16_t max_frame, uint16_t max_route);

  // Reads the index and every slot's header. False when there is no index, or one
  // written for another slot count: the caller then decides whether to create().
  bool open();
  // Clears every slot and writes an empty index; keeps the sequence going.
  bool create();
  bool usable() const { return _usable; }
  void disable() { _usable = false; }

  uint32_t nextSeq() const { return _next; }
  uint32_t acked() const { return _acked; }
  // Hands out the next sequence number. Works whether or not the ring is usable,
  // so the caller can number its queue the same way in both cases.
  uint32_t takeSeq() { return _next++; }

  // Writes a record to slot seq % slots and reads it back. A record that does not
  // read back whole is removed and reported as a failure.
  bool put(const Record& r, const uint8_t* frame, const uint8_t* route);
  // Reads the record for `seq` if the ring still holds it. frame/route must hold
  // max_frame/max_route bytes; either may be null to skip it.
  bool get(uint32_t seq, Record& r, uint8_t* frame, uint8_t* route);
  // Seqs still held and not yet consumed (acked <= seq < next), oldest first.
  int pending(uint32_t* out, int max) const;

  // Every seq below `seq` is consumed. Never moves backwards.
  void setAcked(uint32_t seq);
  bool indexDirty() const { return _dirty; }
  bool flushIndex();

  uint8_t errors() const { return _errors; }

private:
  void failed();
  bool writeIndexNow();

  Storage& _storage;
  uint16_t _slots;
  uint32_t* _slot_seq;
  uint8_t* _scratch;
  size_t _scratch_cap;
  uint16_t _max_frame;
  uint16_t _max_route;
  uint32_t _next = 0;
  uint32_t _acked = 0;
  bool _usable = false;
  bool _dirty = false;
  uint8_t _errors = 0;
};

}  // namespace offline_ring
