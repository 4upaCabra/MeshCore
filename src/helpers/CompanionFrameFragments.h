#pragma once

#include <helpers/OptionalFeatureFlags.h>

#include <stddef.h>
#include <stdint.h>

namespace companion_frames {

#ifdef WITH_COMPANION_FRAME_FRAGMENTS

static constexpr uint8_t FRAME_TYPE = 0xED;
static constexpr uint8_t VERSION[4] = {'F', 'R', '0', '1'};
static constexpr uint8_t FLAG_QUEUED = 0x01;
static constexpr uint8_t KNOWN_FLAGS = FLAG_QUEUED;
static constexpr uint16_t HEADER_LEN = 15;
static constexpr uint16_t MAX_FRAME_LEN = 176;
static constexpr uint16_t CHUNK_LEN = MAX_FRAME_LEN - HEADER_LEN;
static constexpr uint16_t MAX_ORIGINAL_LEN = 259;

uint8_t fragmentCount(uint16_t original_len);
uint16_t fragmentOffset(uint8_t index);
uint16_t fragmentChunkLength(uint16_t original_len, uint8_t index);

size_t buildFragment(uint8_t* dest, size_t dest_size,
                     const uint8_t* original, uint16_t original_len,
                     uint16_t fragment_id, uint8_t index, uint8_t flags);

#endif  // WITH_COMPANION_FRAME_FRAGMENTS

}  // namespace companion_frames
