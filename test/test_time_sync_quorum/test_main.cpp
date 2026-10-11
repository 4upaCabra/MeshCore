#include <gtest/gtest.h>

#include <helpers/TimeSyncQuorum.h>

using namespace timesync;

namespace {

const uint32_t T0 = 1790000000u;  // 2026-09 UTC
const Params P = { 60, 120, 36000, 300 };

struct Buf {
  Sample s[BUF_SIZE] = {};
  int n = 0;
  void add(uint32_t ts, uint32_t sender) { push(s, n, ts, sender); }
};

// count distinct senders reporting ts
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
  EXPECT_EQ(b.s[0].ts, T0 + 7);
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
  EXPECT_EQ(b.s[0].ts, T0 + 3);
  EXPECT_EQ(b.s[BUF_SIZE - 1].ts, T0 + BUF_SIZE + 2);
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
  Result r = evaluate(b.s, b.n, T0, true, P);
  EXPECT_EQ(r.decision, Decision::NONE);
}

TEST(TimeSyncQuorum, UnsetClockTakesQuorumOfThreeInAnyDirection) {
  Buf b = agreeing(3, T0);
  Result r = evaluate(b.s, b.n, T0 + 100000, true, P);
  EXPECT_EQ(r.decision, Decision::APPLY);
  EXPECT_EQ(r.median, T0 + 1);
}

TEST(TimeSyncQuorum, SetClockNeedsSevenSenders) {
  Buf b = agreeing(6, T0 + 1000);
  EXPECT_EQ(evaluate(b.s, b.n, T0, false, P).decision, Decision::NONE);
  b.add(T0 + 1006, 999);
  EXPECT_EQ(evaluate(b.s, b.n, T0, false, P).decision, Decision::APPLY);
}

TEST(TimeSyncQuorum, SmallDriftIsIgnored) {
  Buf b = agreeing(7, T0 + 100);
  EXPECT_EQ(evaluate(b.s, b.n, T0, false, P).decision, Decision::NONE);
}

TEST(TimeSyncQuorum, ForwardStepAboveMaxJumpIsIgnored) {
  Buf b = agreeing(7, T0 + 40000);
  EXPECT_EQ(evaluate(b.s, b.n, T0, false, P).decision, Decision::NONE);
}

TEST(TimeSyncQuorum, SmallBackwardStepIsApplied) {
  Buf b = agreeing(7, T0 - 200);  // fast RC oscillator, ~200 s ahead
  Result r = evaluate(b.s, b.n, T0, false, P);
  EXPECT_EQ(r.decision, Decision::APPLY);
  EXPECT_EQ(r.median, T0 - 197);
}

TEST(TimeSyncQuorum, LargeBackwardStepIsRefused) {
  Buf b = agreeing(7, T0 - 3600);
  Result r = evaluate(b.s, b.n, T0, false, P);
  EXPECT_EQ(r.decision, Decision::REFUSED_BACK);
  EXPECT_EQ(r.median, T0 - 3597);
}

TEST(TimeSyncQuorum, OutliersOutsideClusterDoNotCount) {
  Buf b = agreeing(7, T0 + 1000);
  b.add(T0 + 9000, 500);
  b.add(T0 - 9000, 501);
  Result r = evaluate(b.s, b.n, T0, false, P);
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

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
