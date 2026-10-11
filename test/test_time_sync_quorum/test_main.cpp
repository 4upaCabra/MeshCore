#include <gtest/gtest.h>

#include <helpers/TimeSyncQuorum.h>

using namespace timesync;

namespace {

const uint32_t T0 = 1790000000u;  // 2026-09 UTC
const uint32_t HOUR = 3600u;
const Params P = { 60, 120, 36000, 300, 12 * HOUR, 2 };

struct Buf {
  Sample s[BUF_SIZE] = {};
  int n = 0;
  uint32_t now = 0;

  // Add a sample whose sender claimed `ts` when our local clock read `local`,
  // then make our local "now" equal `now_local`.
  void addRaw(uint32_t ts, uint32_t sender, uint32_t local, uint32_t now_local) {
    push(s, n, (int64_t)ts - (int64_t)local, local, sender);
    now = now_local;
  }
  // Burst sample: the sender claims `ts` right now.
  void add(uint32_t ts, uint32_t sender) { addRaw(ts, sender, now, now); }
};

// count distinct senders reporting ts, all received "now"
Buf agreeing(int count, uint32_t ts) {
  Buf b;
  for (int i = 0; i < count; i++) b.add(ts + i, 100 + i);
  return b;
}

}  // namespace

TEST(TimeSyncQuorum, SameSenderKeepsOnlyLatestSample) {
  Buf b;
  for (int i = 0; i < 8; i++) b.add(T0 + i, 42);
  ASSERT_EQ(b.n, 1);
  EXPECT_EQ(b.s[0].offset, (int64_t)(T0 + 7));
}

TEST(TimeSyncQuorum, UnknownSendersAreNotMerged) {
  Buf b;
  for (int i = 0; i < 4; i++) b.add(T0 + i, 0);
  EXPECT_EQ(b.n, 4);
}

TEST(TimeSyncQuorum, FullBufferDropsOldest) {
  Buf b;
  for (int i = 0; i < BUF_SIZE + 3; i++) b.add(T0 + i, 100 + i);
  ASSERT_EQ(b.n, BUF_SIZE);
  EXPECT_EQ(b.s[0].offset, (int64_t)(T0 + 3));
  EXPECT_EQ(b.s[BUF_SIZE - 1].offset, (int64_t)(T0 + BUF_SIZE + 2));
}

TEST(TimeSyncQuorum, ResentSampleMovesToNewestSlot) {
  Buf b;
  b.add(T0, 1);
  b.add(T0 + 1, 2);
  b.add(T0 + 2, 1);
  ASSERT_EQ(b.n, 2);
  EXPECT_EQ(b.s[0].sender, 2u);
  EXPECT_EQ(b.s[1].sender, 1u);
}

// The bug this replaces: one node adverting repeatedly filled the buffer and
// reached quorum on its own.
TEST(TimeSyncQuorum, OneChattyNodeCannotSetClock) {
  Buf b;
  for (int i = 0; i < 20; i++) b.add(T0 + 5000 + i, 7);
  Result r = evaluate(b.s, b.n, T0, b.now, true, P);
  EXPECT_EQ(r.decision, Decision::NONE);
}

TEST(TimeSyncQuorum, UnsetClockTakesQuorumOfThreeInAnyDirection) {
  Buf b = agreeing(3, T0);
  Result r = evaluate(b.s, b.n, T0 + 100000, b.now, true, P);
  EXPECT_EQ(r.decision, Decision::APPLY);
  EXPECT_EQ(r.median, T0 + 1);
}

TEST(TimeSyncQuorum, SetClockNeedsSevenSenders) {
  Buf b = agreeing(6, T0 + 1000);
  EXPECT_EQ(evaluate(b.s, b.n, T0, b.now, false, P).decision, Decision::NONE);
  b.add(T0 + 1006, 999);
  EXPECT_EQ(evaluate(b.s, b.n, T0, b.now, false, P).decision, Decision::APPLY);
}

TEST(TimeSyncQuorum, SmallDriftIsIgnored) {
  Buf b = agreeing(7, T0 + 100);
  EXPECT_EQ(evaluate(b.s, b.n, T0, b.now, false, P).decision, Decision::NONE);
}

TEST(TimeSyncQuorum, ForwardStepAboveMaxJumpIsIgnored) {
  Buf b = agreeing(7, T0 + 40000);
  EXPECT_EQ(evaluate(b.s, b.n, T0, b.now, false, P).decision, Decision::NONE);
}

TEST(TimeSyncQuorum, SmallBackwardStepIsApplied) {
  Buf b = agreeing(7, T0 - 200);  // fast RC oscillator, ~200 s ahead
  Result r = evaluate(b.s, b.n, T0, b.now, false, P);
  EXPECT_EQ(r.decision, Decision::APPLY);
  EXPECT_EQ(r.median, T0 - 197);
}

TEST(TimeSyncQuorum, LargeBackwardStepIsRefused) {
  Buf b = agreeing(7, T0 - 3600);
  Result r = evaluate(b.s, b.n, T0, b.now, false, P);
  EXPECT_EQ(r.decision, Decision::REFUSED_BACK);
  EXPECT_EQ(r.median, T0 - 3597);
}

TEST(TimeSyncQuorum, OutliersOutsideClusterDoNotCount) {
  Buf b = agreeing(7, T0 + 1000);
  b.add(T0 + 9000, 500);
  b.add(T0 - 9000, 501);
  Result r = evaluate(b.s, b.n, T0, b.now, false, P);
  EXPECT_EQ(r.cluster, 7);
  EXPECT_EQ(r.decision, Decision::APPLY);
}

TEST(TimeSyncQuorum, ChannelSenderFromNamePrefix) {
  uint32_t a = senderFromChannelText("Alice: hi");
  EXPECT_NE(a, 0u);
  EXPECT_EQ(a, senderFromChannelText("Alice: another message"));
  EXPECT_NE(a, senderFromChannelText("Bob: hi"));
  EXPECT_EQ(senderFromChannelText("no prefix here"), 0u);
  EXPECT_EQ(senderFromChannelText(": empty name"), 0u);
}

// ---------------------------------------------------------------------------
// Quiet mesh: samples spread over hours must still agree after being aged.
// ---------------------------------------------------------------------------

TEST(TimeSyncQuorum, QuietMeshTwoSendersOverHoursSetUntrustedClock) {
  Buf b;
  b.addRaw(T0, 1, 0, 0);                          // peer A, offset T0
  b.addRaw(T0 + 3 * HOUR, 2, 3 * HOUR, 3 * HOUR); // peer B, 3 h later, offset T0
  Result r = evaluate(b.s, b.n, 0, b.now, true, P);
  EXPECT_EQ(r.cluster, 2);
  EXPECT_EQ(r.decision, Decision::APPLY);
  EXPECT_EQ(r.median, T0 + 3 * HOUR);  // projected to now
}

TEST(TimeSyncQuorum, QuietMeshThreeSendersOverHoursSetUntrustedClock) {
  Buf b;
  b.addRaw(T0, 1, 0, 0);
  b.addRaw(T0 + 2 * HOUR, 2, 2 * HOUR, 2 * HOUR);
  b.addRaw(T0 + 5 * HOUR, 3, 5 * HOUR, 5 * HOUR);
  Result r = evaluate(b.s, b.n, 0, b.now, true, P);
  EXPECT_EQ(r.cluster, 3);
  EXPECT_EQ(r.decision, Decision::APPLY);
  EXPECT_EQ(r.median, T0 + 5 * HOUR);
}

// A genuinely sparse mesh (no more peers than the low quorum heard in the aged
// window) may use the lower quorum, even when both happened to advert recently.
TEST(TimeSyncQuorum, TwoSendersAreEnoughWhenTheMeshIsSparse) {
  Buf b = agreeing(2, T0);
  Result r = evaluate(b.s, b.n, 0, b.now, true, P);
  EXPECT_EQ(r.active, 2);
  EXPECT_EQ(r.cluster, 2);
  EXPECT_EQ(r.decision, Decision::APPLY);
}

// Three peers heard, only two agree: the busy three-sender rule stands.
TEST(TimeSyncQuorum, TwoAgreeingSendersAmongThreeDoNotDowngrade) {
  Buf b;
  b.add(T0, 1);
  b.add(T0 + 1, 2);
  b.add(T0 + 5000, 3);   // third peer disagrees
  Result r = evaluate(b.s, b.n, 0, b.now, true, P);
  EXPECT_EQ(r.active, 3);
  EXPECT_EQ(r.cluster, 2);
  EXPECT_EQ(r.decision, Decision::NONE);
}

TEST(TimeSyncQuorum, OneSenderRepeatingOverHoursCannotSetClock) {
  Buf b;
  for (int i = 0; i < 6; i++) b.addRaw(T0 + i * HOUR, 7, i * HOUR, i * HOUR);
  ASSERT_EQ(b.n, 1);
  EXPECT_EQ(evaluate(b.s, b.n, 0, b.now, true, P).decision, Decision::NONE);
}

TEST(TimeSyncQuorum, SamplesOlderThanMaxAgeAreIgnored) {
  Buf b;
  b.addRaw(T0, 1, 0, 0);
  b.addRaw(T0 + 13 * HOUR, 2, 13 * HOUR, 13 * HOUR);  // A is now 13 h old
  Result r = evaluate(b.s, b.n, 0, b.now, true, P);
  EXPECT_EQ(r.active, 1);
  EXPECT_EQ(r.decision, Decision::NONE);
}

TEST(TimeSyncQuorum, BackwardRefusalHoldsWithAgedSamples) {
  Buf b;
  for (int i = 0; i < 7; i++) {
    uint32_t local = i * HOUR;
    b.addRaw(T0 + local, 100 + i, local, local);  // all offsets are T0
  }
  // projected = T0 + 6 h; the running clock is 2 h ahead of that.
  Result r = evaluate(b.s, b.n, T0 + 6 * HOUR + 7200, b.now, false, P);
  EXPECT_EQ(r.cluster, 7);
  EXPECT_EQ(r.decision, Decision::REFUSED_BACK);
}

TEST(TimeSyncQuorum, SmallBackwardStepAppliesWithAgedSamples) {
  Buf b;
  for (int i = 0; i < 7; i++) {
    uint32_t local = i * HOUR;
    b.addRaw(T0 + local, 100 + i, local, local);
  }
  Result r = evaluate(b.s, b.n, T0 + 6 * HOUR + 200, b.now, false, P);
  EXPECT_EQ(r.decision, Decision::APPLY);
  EXPECT_EQ(r.median, T0 + 6 * HOUR);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
