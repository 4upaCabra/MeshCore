#include <gtest/gtest.h>

#include <helpers/mcoimg_detect/MCOImgDetect.h>

TEST(MCOImgDetect, RecognisesTextVersions) {
  mcoimg_detect::Meta meta;
  ASSERT_TRUE(mcoimg_detect::parseText("im3:abc", meta));
  EXPECT_EQ(meta.form, mcoimg_detect::Form::VersionedText);
  EXPECT_EQ(meta.version, 3);
  EXPECT_TRUE(meta.has_explicit_version);

  ASSERT_TRUE(mcoimg_detect::parseText(" im:abc", meta));
  EXPECT_EQ(meta.form, mcoimg_detect::Form::LegacyText);
  EXPECT_FALSE(meta.has_explicit_version);
  EXPECT_FALSE(mcoimg_detect::parseText("im3:", meta));
  EXPECT_FALSE(mcoimg_detect::parseText("im: plain text", meta));
  EXPECT_FALSE(mcoimg_detect::parseText("im3:abc def", meta));
  EXPECT_FALSE(mcoimg_detect::parseText("plain", meta));
}

TEST(MCOImgDetect, RecognisesBinaryEnvelope) {
  const uint8_t data[] = {3, 'B', 'o', 'b', 0x14, 0xAA};
  mcoimg_detect::Meta meta;
  ASSERT_TRUE(mcoimg_detect::parseBinaryEnvelope(0x0120, data, sizeof(data), meta));
  EXPECT_EQ(meta.form, mcoimg_detect::Form::Binary);
  EXPECT_EQ(meta.version, 4);
  EXPECT_STREQ(meta.sender, "Bob");
  EXPECT_FALSE(mcoimg_detect::parseBinaryEnvelope(0x0121, data, sizeof(data), meta));
}

TEST(MCOImgDetect, RejectsMalformedBinaryEnvelope) {
  const uint8_t wrong_subtype[] = {0, 0x24, 0xAA};
  const uint8_t missing_body[] = {0, 0x14};
  const uint8_t bad_name[] = {4, 'B', 'o', 'b'};
  const uint8_t huge_name[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x0F, 0x14, 0xAA};
  mcoimg_detect::Meta meta;
  EXPECT_FALSE(mcoimg_detect::parseBinaryEnvelope(0x0120, wrong_subtype,
                                                   sizeof(wrong_subtype), meta));
  EXPECT_FALSE(mcoimg_detect::parseBinaryEnvelope(0x0120, missing_body,
                                                   sizeof(missing_body), meta));
  EXPECT_FALSE(mcoimg_detect::parseBinaryEnvelope(0x0120, bad_name,
                                                   sizeof(bad_name), meta));
  EXPECT_FALSE(mcoimg_detect::parseBinaryEnvelope(0x0120, huge_name,
                                                   sizeof(huge_name), meta));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
