#include <gtest/gtest.h>
#include <helpers/OfflineRecordRing.h>

#include <map>
#include <vector>

using namespace offline_ring;

namespace {

// Blobs in memory. `tear_next` makes the next slot write store only part of the
// record while still reporting success — what a full filesystem does on nRF52.
struct MemStorage : public Storage {
  std::map<uint16_t, std::vector<uint8_t>> slots;
  std::vector<uint8_t> index;
  int tear_next = 0;
  bool fail_writes = false;

  bool writeSlot(uint16_t slot, const uint8_t* data, size_t len) override {
    if (fail_writes) return false;
    size_t keep = len;
    if (tear_next > 0) { tear_next--; keep = len / 2; }
    slots[slot].assign(data, data + keep);
    return true;
  }
  int readSlot(uint16_t slot, uint8_t* buf, size_t cap) override {
    auto it = slots.find(slot);
    if (it == slots.end()) return -1;
    const size_t n = it->second.size() < cap ? it->second.size() : cap;
    memcpy(buf, it->second.data(), n);
    return (int)n;
  }
  void removeSlot(uint16_t slot) override { slots.erase(slot); }
  bool writeIndex(const uint8_t* data, size_t len) override {
    if (fail_writes) return false;
    index.assign(data, data + len);
    return true;
  }
  int readIndex(uint8_t* buf, size_t cap) override {
    if (index.empty()) return -1;
    const size_t n = index.size() < cap ? index.size() : cap;
    memcpy(buf, index.data(), n);
    return (int)n;
  }
};

constexpr uint16_t SLOTS = 6;
constexpr uint16_t MAX_FRAME = 64;
constexpr uint16_t MAX_ROUTE = 32;

struct Fixture {
  MemStorage storage;
  uint32_t slot_seq[SLOTS];
  uint8_t scratch[RECORD_HEADER + MAX_FRAME + MAX_ROUTE];
  Ring ring{storage, SLOTS, slot_seq, scratch, sizeof(scratch), MAX_FRAME, MAX_ROUTE};
};

// Puts a frame whose bytes all equal the low byte of its seq, with a route when asked.
bool putFrame(Ring& ring, uint32_t seq, bool with_route = false) {
  uint8_t frame[8];
  memset(frame, (uint8_t)seq, sizeof(frame));
  uint8_t route[4] = { 0x10, 0xB0, 0x15, (uint8_t)seq };
  Record r{seq, 1700000000u + seq, 0, sizeof(frame), (uint16_t)(with_route ? sizeof(route) : 0)};
  return ring.put(r, frame, with_route ? route : nullptr);
}

std::vector<uint32_t> pendingOf(const Ring& ring) {
  uint32_t out[16];
  const int n = ring.pending(out, 16);
  return std::vector<uint32_t>(out, out + n);
}

}  // namespace

TEST(OfflineRecordRing, RecordRoundTripAndCrc) {
  uint8_t frame[5] = { 17, 1, 2, 3, 4 };
  uint8_t route[3] = { 0x20, 0xC4, 0x15 };
  uint8_t buf[RECORD_HEADER + 8];
  Record r{42, 123, 0x81, sizeof(frame), sizeof(route)};
  const size_t len = encodeRecord(buf, sizeof(buf), r, frame, route);
  ASSERT_EQ(len, RECORD_HEADER + 8);

  Record back;
  ASSERT_TRUE(decodeRecord(buf, len, MAX_FRAME, MAX_ROUTE, back));
  EXPECT_EQ(back.seq, 42u);
  EXPECT_EQ(back.at_s, 123u);
  EXPECT_EQ(back.flags, 0x81);
  EXPECT_EQ(back.frame_len, 5);
  EXPECT_EQ(back.route_len, 3);

  // A single flipped bit anywhere, a short blob and a length over the limit all
  // read as "no record".
  buf[RECORD_HEADER + 6] ^= 0x01;
  EXPECT_FALSE(decodeRecord(buf, len, MAX_FRAME, MAX_ROUTE, back));
  buf[RECORD_HEADER + 6] ^= 0x01;
  EXPECT_FALSE(decodeRecord(buf, len - 1, MAX_FRAME, MAX_ROUTE, back));
  EXPECT_FALSE(decodeRecord(buf, len, 4, MAX_ROUTE, back));
  EXPECT_EQ(encodeRecord(buf, RECORD_HEADER + 7, r, frame, route), 0u);
}

TEST(OfflineRecordRing, AFreshRingHoldsNothing) {
  Fixture f;
  EXPECT_FALSE(f.ring.open());   // no index yet
  ASSERT_TRUE(f.ring.create());
  EXPECT_TRUE(f.ring.usable());
  EXPECT_TRUE(pendingOf(f.ring).empty());
  EXPECT_EQ(f.ring.takeSeq(), 0u);
  EXPECT_EQ(f.ring.takeSeq(), 1u);
}

TEST(OfflineRecordRing, PutGetAndRoute) {
  Fixture f;
  ASSERT_TRUE(f.ring.create());
  ASSERT_TRUE(putFrame(f.ring, f.ring.takeSeq(), true));
  ASSERT_TRUE(putFrame(f.ring, f.ring.takeSeq(), false));

  Record r;
  uint8_t frame[MAX_FRAME];
  uint8_t route[MAX_ROUTE];
  ASSERT_TRUE(f.ring.get(0, r, frame, route));
  EXPECT_EQ(r.route_len, 4);
  EXPECT_EQ(route[3], 0);
  ASSERT_TRUE(f.ring.get(1, r, frame, route));
  EXPECT_EQ(r.route_len, 0);
  EXPECT_EQ(frame[0], 1);
  EXPECT_EQ(pendingOf(f.ring), (std::vector<uint32_t>{0, 1}));
}

// A slot taken over by a later record never answers for the earlier one.
TEST(OfflineRecordRing, OverwrittenSlotIsNotMistakenForTheOldRecord) {
  Fixture f;
  ASSERT_TRUE(f.ring.create());
  for (uint32_t i = 0; i < SLOTS + 2; i++) ASSERT_TRUE(putFrame(f.ring, f.ring.takeSeq()));
  Record r;
  EXPECT_FALSE(f.ring.get(0, r, nullptr, nullptr));
  EXPECT_FALSE(f.ring.get(1, r, nullptr, nullptr));
  EXPECT_TRUE(f.ring.get(SLOTS, r, nullptr, nullptr));
  EXPECT_EQ(pendingOf(f.ring), (std::vector<uint32_t>{2, 3, 4, 5, 6, 7}));
}

// The watermark is what the caller says is consumed, so an entry dropped out of
// order cannot shift the window: it simply stays below or above the mark.
TEST(OfflineRecordRing, WatermarkSurvivesReopenAndOrdersByseq) {
  Fixture f;
  ASSERT_TRUE(f.ring.create());
  for (uint32_t i = 0; i < 5; i++) ASSERT_TRUE(putFrame(f.ring, f.ring.takeSeq()));
  f.ring.setAcked(3);
  EXPECT_TRUE(f.ring.indexDirty());
  ASSERT_TRUE(f.ring.flushIndex());
  f.ring.setAcked(2);   // never backwards
  EXPECT_FALSE(f.ring.indexDirty());

  uint32_t slot_seq[SLOTS];
  uint8_t scratch[RECORD_HEADER + MAX_FRAME + MAX_ROUTE];
  Ring again(f.storage, SLOTS, slot_seq, scratch, sizeof(scratch), MAX_FRAME, MAX_ROUTE);
  ASSERT_TRUE(again.open());
  EXPECT_EQ(again.acked(), 3u);
  EXPECT_EQ(again.nextSeq(), 5u);
  EXPECT_EQ(pendingOf(again), (std::vector<uint32_t>{3, 4}));
  EXPECT_EQ(again.takeSeq(), 5u);
}

// The index trails the records: a record written after the last index flush
// still counts, and the sequence never restarts below it.
TEST(OfflineRecordRing, RecordsNewerThanTheIndexAreFound) {
  Fixture f;
  ASSERT_TRUE(f.ring.create());   // index says next = 0
  for (uint32_t i = 0; i < 4; i++) ASSERT_TRUE(putFrame(f.ring, f.ring.takeSeq()));

  uint32_t slot_seq[SLOTS];
  uint8_t scratch[RECORD_HEADER + MAX_FRAME + MAX_ROUTE];
  Ring again(f.storage, SLOTS, slot_seq, scratch, sizeof(scratch), MAX_FRAME, MAX_ROUTE);
  ASSERT_TRUE(again.open());
  EXPECT_EQ(again.nextSeq(), 4u);
  EXPECT_EQ(pendingOf(again), (std::vector<uint32_t>{0, 1, 2, 3}));
}

TEST(OfflineRecordRing, LappedRingRestoresTheNewestWindowInOrder) {
  Fixture f;
  ASSERT_TRUE(f.ring.create());
  for (uint32_t i = 0; i < 3 * SLOTS + 1; i++) ASSERT_TRUE(putFrame(f.ring, f.ring.takeSeq()));
  uint32_t slot_seq[SLOTS];
  uint8_t scratch[RECORD_HEADER + MAX_FRAME + MAX_ROUTE];
  Ring again(f.storage, SLOTS, slot_seq, scratch, sizeof(scratch), MAX_FRAME, MAX_ROUTE);
  ASSERT_TRUE(again.open());
  EXPECT_EQ(pendingOf(again), (std::vector<uint32_t>{13, 14, 15, 16, 17, 18}));

  // A short output keeps the oldest of them.
  uint32_t out[3];
  ASSERT_EQ(again.pending(out, 3), 3);
  EXPECT_EQ(out[0], 13u);
  EXPECT_EQ(out[2], 15u);
}

TEST(OfflineRecordRing, TornWriteIsRejectedAndRepeatedFailuresDisable) {
  Fixture f;
  ASSERT_TRUE(f.ring.create());
  ASSERT_TRUE(putFrame(f.ring, f.ring.takeSeq()));
  f.storage.tear_next = 1;
  EXPECT_FALSE(putFrame(f.ring, f.ring.takeSeq()));
  EXPECT_EQ(f.storage.slots.count(1), 0u);   // the half record is not left behind
  EXPECT_TRUE(f.ring.usable());
  EXPECT_EQ(pendingOf(f.ring), (std::vector<uint32_t>{0}));

  f.storage.fail_writes = true;
  EXPECT_FALSE(putFrame(f.ring, f.ring.takeSeq()));
  EXPECT_FALSE(putFrame(f.ring, f.ring.takeSeq()));
  EXPECT_FALSE(putFrame(f.ring, f.ring.takeSeq()));
  EXPECT_FALSE(f.ring.usable());
}

TEST(OfflineRecordRing, TornRecordOnDiskReadsAsAbsent) {
  Fixture f;
  ASSERT_TRUE(f.ring.create());
  ASSERT_TRUE(putFrame(f.ring, f.ring.takeSeq()));
  ASSERT_TRUE(putFrame(f.ring, f.ring.takeSeq()));
  f.storage.slots[1][RECORD_HEADER + 2] ^= 0xFF;   // damage the body, keep the header

  uint32_t slot_seq[SLOTS];
  uint8_t scratch[RECORD_HEADER + MAX_FRAME + MAX_ROUTE];
  Ring again(f.storage, SLOTS, slot_seq, scratch, sizeof(scratch), MAX_FRAME, MAX_ROUTE);
  ASSERT_TRUE(again.open());
  Record r;
  EXPECT_TRUE(again.get(0, r, nullptr, nullptr));
  EXPECT_FALSE(again.get(1, r, nullptr, nullptr));
}

TEST(OfflineRecordRing, IndexForAnotherGeometryIsRejected) {
  Fixture f;
  ASSERT_TRUE(f.ring.create());
  ASSERT_TRUE(putFrame(f.ring, f.ring.takeSeq()));

  uint32_t slot_seq[SLOTS + 2];
  uint8_t scratch[RECORD_HEADER + MAX_FRAME + MAX_ROUTE];
  Ring bigger(f.storage, SLOTS + 2, slot_seq, scratch, sizeof(scratch), MAX_FRAME, MAX_ROUTE);
  EXPECT_FALSE(bigger.open());
  EXPECT_FALSE(bigger.usable());
}

TEST(OfflineRecordRing, RecordInTheWrongSlotIsIgnored) {
  Fixture f;
  ASSERT_TRUE(f.ring.create());
  ASSERT_TRUE(putFrame(f.ring, f.ring.takeSeq()));
  f.storage.slots[3] = f.storage.slots[0];   // seq 0 does not belong in slot 3

  uint32_t slot_seq[SLOTS];
  uint8_t scratch[RECORD_HEADER + MAX_FRAME + MAX_ROUTE];
  Ring again(f.storage, SLOTS, slot_seq, scratch, sizeof(scratch), MAX_FRAME, MAX_ROUTE);
  ASSERT_TRUE(again.open());
  EXPECT_EQ(pendingOf(again), (std::vector<uint32_t>{0}));
}

// An index claiming more was consumed than was ever written is damaged: every
// record held comes back rather than none.
TEST(OfflineRecordRing, DamagedWatermarkReplaysEverything) {
  Fixture f;
  ASSERT_TRUE(f.ring.create());
  for (uint32_t i = 0; i < 3; i++) ASSERT_TRUE(putFrame(f.ring, f.ring.takeSeq()));
  f.storage.index[8] = 0xFF;    // acked, low byte
  f.storage.index[11] = 0x7F;   // acked, high byte: far beyond anything written

  uint32_t slot_seq[SLOTS];
  uint8_t scratch[RECORD_HEADER + MAX_FRAME + MAX_ROUTE];
  Ring again(f.storage, SLOTS, slot_seq, scratch, sizeof(scratch), MAX_FRAME, MAX_ROUTE);
  ASSERT_TRUE(again.open());
  EXPECT_EQ(again.acked(), 0u);
  EXPECT_EQ(pendingOf(again), (std::vector<uint32_t>{0, 1, 2}));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
