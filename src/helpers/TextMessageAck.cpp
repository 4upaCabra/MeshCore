#include "TextMessageAck.h"

#include <string.h>

#include <Utils.h>

namespace text_message_ack {
namespace {

// MAX_TEXT_LEN of BaseChatMesh.h, which composeMsgPacket holds a text to
// before it computes the hash; a guard here, since the helper hashes any
// C string it is given.
const size_t kMaxTextLen = 10 * CIPHER_BLOCK_SIZE;

}  // namespace

uint32_t expectedAck(uint32_t timestamp, uint8_t attempt, const char* text,
                     const uint8_t* self_pub_key) {
  uint8_t temp[5 + kMaxTextLen];
  size_t text_len = strlen(text);
  if (text_len > kMaxTextLen) text_len = kMaxTextLen;
  memcpy(temp, &timestamp, 4);
  temp[4] = (uint8_t)(attempt & 3);
  memcpy(&temp[5], text, text_len);
  uint32_t ack;
  mesh::Utils::sha256((uint8_t*)&ack, 4, temp, (int)(5 + text_len),
                      self_pub_key, PUB_KEY_SIZE);
  return ack;
}

bool textTransformed(const char* original, const char* sent) {
  return original != sent && strcmp(original, sent) != 0;
}

}  // namespace text_message_ack
