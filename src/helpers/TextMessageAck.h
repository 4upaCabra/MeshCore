#pragma once

#include <stdint.h>

namespace text_message_ack {

// The acknowledgement hash of a direct text message, as the recipient
// computes it and BaseChatMesh::composeMsgPacket expects it: the first four
// bytes of SHA-256 over the timestamp, the attempt number's two low bits (the
// text type bits above them are zero for a plain text), the text without its
// terminator, and the sender's public key. Attempt numbers from four on travel
// behind the text and stay out of the hash, so attempts k and k+4 of one
// message share a hash.
uint32_t expectedAck(uint32_t timestamp, uint8_t attempt, const char* text,
                     const uint8_t* self_pub_key);

// True when the node sends [sent] where the app handed over [original]: the
// text was transformed (transliteration and the like), so the acknowledgement
// on the air names a text the app does not know, and an app that computes the
// hash itself, rather than taking it from RESP_CODE_SENT, waits for one that
// never comes. A text the app had transformed the same way already is not
// transformed.
bool textTransformed(const char* original, const char* sent);

}  // namespace text_message_ack
