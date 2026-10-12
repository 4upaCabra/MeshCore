#include <gtest/gtest.h>
#include <helpers/TextMessageAck.h>
#include <Utils.h>
#include <string.h>

using namespace text_message_ack;

// What the companion needs of the hash helper, so that the hash it reports
// for a text it sends transformed is the one the app computes: the helper
// hashes exactly the bytes BaseChatMesh::composeMsgPacket hashes, only the
// attempt's two low bits count, the terminator stays out, and a text is
// transformed when it differs in any byte. The native host runs a
// deterministic stand-in for SHA-256 whose four-byte digest shows only some
// byte positions (the attempt byte and the key are not among them), so the
// reference below assembles the same bytes and runs them through the same
// function, and the inequalities use inputs the stand-in does show.

static const uint8_t kKey[PUB_KEY_SIZE] = {
    0xaf, 0xda, 0xd9, 0x5d, 0x3a, 0x1e, 0x3e, 0xe4, 0xc5, 0x15, 0x2e,
    0x45, 0xdb, 0xfd, 0x89, 0xb3, 0xef, 0x58, 0x4d, 0x54, 0x2c, 0x95,
    0x8e, 0x90, 0xc4, 0xb9, 0x0f, 0xeb, 0x6a, 0x1b, 0xc0, 0x23};

static uint32_t reference(uint32_t timestamp, uint8_t attempt_bits,
                          const char* text, const uint8_t* key) {
  uint8_t temp[5 + 160];
  size_t len = strlen(text);
  memcpy(temp, &timestamp, 4);
  temp[4] = attempt_bits;
  memcpy(&temp[5], text, len);
  uint32_t ack;
  mesh::Utils::sha256((uint8_t*)&ack, 4, temp, (int)(5 + len), key,
                      PUB_KEY_SIZE);
  return ack;
}

TEST(TextMessageAck, HashesTimestampAttemptBitsTextAndKey) {
  EXPECT_EQ(expectedAck(0x6ac84010u, 0, "тест отправка 1", kKey),
            reference(0x6ac84010u, 0, "тест отправка 1", kKey));
  EXPECT_EQ(expectedAck(0x6ac84010u, 1, "hi", kKey),
            reference(0x6ac84010u, 1, "hi", kKey));
  EXPECT_EQ(expectedAck(0x6ac84010u, 0, "", kKey),
            reference(0x6ac84010u, 0, "", kKey));
}

TEST(TextMessageAck, OnlyTheTwoLowBitsOfTheAttemptCount) {
  EXPECT_EQ(expectedAck(1, 4, "hi", kKey), expectedAck(1, 0, "hi", kKey));
  EXPECT_EQ(expectedAck(1, 5, "hi", kKey), expectedAck(1, 1, "hi", kKey));
  EXPECT_EQ(expectedAck(1, 7, "hi", kKey), expectedAck(1, 3, "hi", kKey));
  EXPECT_EQ(expectedAck(1, 4, "hi", kKey), reference(1, 0, "hi", kKey));
  EXPECT_EQ(expectedAck(1, 6, "hi", kKey), reference(1, 2, "hi", kKey));
}

TEST(TextMessageAck, TheTimestampAndTheTextChangeTheHash) {
  EXPECT_NE(expectedAck(1, 0, "hi", kKey), expectedAck(2, 0, "hi", kKey));
  EXPECT_NE(expectedAck(1, 0, "тест отправка 1", kKey),
            expectedAck(1, 0, "тecт oтпpaвкa 1", kKey));
}

TEST(TextMessageAck, TheTerminatorStaysOutOfTheHash) {
  char padded[8] = "hi";
  padded[3] = 'x';  // garbage behind the terminator
  EXPECT_EQ(expectedAck(1, 0, padded, kKey), expectedAck(1, 0, "hi", kKey));
}

TEST(TextMessageAck, TransformedIsAnyDifferenceBetweenTheTexts) {
  const char* same = "hi";
  EXPECT_FALSE(textTransformed(same, same));
  EXPECT_FALSE(textTransformed("hi", "hi"));
  EXPECT_FALSE(textTransformed("тecт oтпpaвкa 1", "тecт oтпpaвкa 1"));
  EXPECT_TRUE(textTransformed("тест отправка 1", "тecт oтпpaвкa 1"));
  EXPECT_TRUE(textTransformed("тест", "TECT"));  // Cyrillic -> Latin
  EXPECT_TRUE(textTransformed("hi", "hi "));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
