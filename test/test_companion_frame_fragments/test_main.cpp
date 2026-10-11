#include <gtest/gtest.h>
#include <helpers/CompanionAppCapabilities.h>
#include <helpers/CompanionFrameFragments.h>
#include <string>

using namespace companion_app;
using namespace companion_frames;

TEST(CompanionFrameFragments, GeometryAndWireHeader) {
  uint8_t original[MAX_ORIGINAL_LEN] = {};
  original[0] = 0x88;
  for (size_t i = 1; i < sizeof(original); ++i) original[i] = static_cast<uint8_t>(i);
  uint8_t out[MAX_FRAME_LEN] = {};
  EXPECT_EQ(fragmentCount(176), 0);
  EXPECT_EQ(fragmentCount(177), 2);
  EXPECT_EQ(fragmentCount(259), 2);
  EXPECT_EQ(fragmentCount(260), 0);
  EXPECT_EQ(buildFragment(out, sizeof(out), original, 259, 0x1234, 0, FLAG_QUEUED), 176u);
  EXPECT_EQ(out[0], 0xED);
  EXPECT_EQ(std::string(reinterpret_cast<char*>(&out[1]), 4), "FR01");
  EXPECT_EQ(out[5], FLAG_QUEUED);
  EXPECT_EQ(out[6], 0x34);
  EXPECT_EQ(out[7], 0x12);
  EXPECT_EQ(out[9], 2);
  EXPECT_EQ(out[10], 0x88);
  EXPECT_EQ(out[11], 0x03);
  EXPECT_EQ(out[12], 0x01);
  EXPECT_EQ(buildFragment(out, sizeof(out), original, 259, 0x1234, 1, 0), 113u);
  EXPECT_EQ(out[13], 161);
  EXPECT_EQ(out[14], 0);
}

TEST(CompanionFrameFragments, RejectsInvalidInputs) {
  uint8_t original[177] = {1};
  uint8_t out[MAX_FRAME_LEN] = {};
  EXPECT_EQ(buildFragment(out, sizeof(out), original, 177, 0, 0, 0), 0u);
  EXPECT_EQ(buildFragment(out, sizeof(out), original, 177, 1, 2, 0), 0u);
  EXPECT_EQ(buildFragment(out, sizeof(out), original, 177, 1, 0, 0x80), 0u);
}

TEST(CompanionAppCapabilities, ParsesBoundedCapabilityTokens) {
  const char exact[] = "MeshCoreOpen;cap=frmfrg1";
  EXPECT_TRUE(hasCapability(reinterpret_cast<const uint8_t*>(exact), sizeof(exact) - 1, "frmfrg1"));
  const char multiple[] = "MeshCoreOpen;cap=frmfrg1,mctxt,mcmp,aeic";
  EXPECT_TRUE(hasCapability(reinterpret_cast<const uint8_t*>(multiple), sizeof(multiple) - 1, "frmfrg1"));
  EXPECT_TRUE(hasCapability(reinterpret_cast<const uint8_t*>(multiple), sizeof(multiple) - 1, "mctxt"));
  EXPECT_TRUE(hasCapability(reinterpret_cast<const uint8_t*>(multiple), sizeof(multiple) - 1, "mcmp"));
  EXPECT_TRUE(hasCapability(reinterpret_cast<const uint8_t*>(multiple), sizeof(multiple) - 1, "aeic"));
  const char wrong[] = "MeshCoreOpen;cap=frmfrg10,mctxt2";
  EXPECT_FALSE(hasCapability(reinterpret_cast<const uint8_t*>(wrong), sizeof(wrong) - 1, "frmfrg1"));
  EXPECT_FALSE(hasCapability(reinterpret_cast<const uint8_t*>(wrong), sizeof(wrong) - 1, "mctxt"));
  const char embedded[] = "MeshCoreOpen;foo=cap=frmfrg1";
  EXPECT_FALSE(hasCapability(reinterpret_cast<const uint8_t*>(embedded), sizeof(embedded) - 1, "frmfrg1"));
  const char other_token[] = "MeshCoreOpen;foo=bar;cap=mctxt";
  EXPECT_TRUE(hasCapability(reinterpret_cast<const uint8_t*>(other_token), sizeof(other_token) - 1, "mctxt"));
  EXPECT_FALSE(hasCapability(nullptr, 0, "frmfrg1"));
}

TEST(CompanionAppCapabilities, RecognizesMeshCoreOpenNameVariants) {
  const char compact[] = "MeshCoreOpen;cap=mctxt";
  EXPECT_TRUE(appNameIsMeshCoreOpen(reinterpret_cast<const uint8_t*>(compact),
                                    sizeof(compact) - 1));
  const char spaced[] = "meshcore open;cap=frmfrg1";
  EXPECT_TRUE(appNameIsMeshCoreOpen(reinterpret_cast<const uint8_t*>(spaced),
                                    sizeof(spaced) - 1));
  const char underscored[] = "meshcore_open";
  EXPECT_TRUE(appNameIsMeshCoreOpen(reinterpret_cast<const uint8_t*>(underscored),
                                    sizeof(underscored) - 1));
  const char other[] = "OtherApp;cap=aeic";
  EXPECT_FALSE(appNameIsMeshCoreOpen(reinterpret_cast<const uint8_t*>(other),
                                     sizeof(other) - 1));
}

TEST(CompanionFrameFragments, HandlesExactBoundaries) {
  EXPECT_EQ(fragmentChunkLength(177, 0), 161);
  EXPECT_EQ(fragmentChunkLength(177, 1), 16);
  EXPECT_EQ(fragmentChunkLength(259, 0), 161);
  EXPECT_EQ(fragmentChunkLength(259, 1), 98);
  EXPECT_EQ(fragmentChunkLength(259, 2), 0);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
