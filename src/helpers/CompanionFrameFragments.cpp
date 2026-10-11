#include <helpers/OptionalFeatureFlags.h>

#include "CompanionFrameFragments.h"

#include <string.h>

namespace companion_frames {

#ifdef WITH_COMPANION_FRAME_FRAGMENTS

uint8_t fragmentCount(uint16_t original_len) {
  if (original_len <= MAX_FRAME_LEN || original_len > MAX_ORIGINAL_LEN) return 0;
  return static_cast<uint8_t>((original_len + CHUNK_LEN - 1) / CHUNK_LEN);
}

uint16_t fragmentOffset(uint8_t index) {
  return static_cast<uint16_t>(index) * CHUNK_LEN;
}

uint16_t fragmentChunkLength(uint16_t original_len, uint8_t index) {
  const uint8_t count = fragmentCount(original_len);
  if (count == 0 || index >= count) return 0;
  const uint16_t offset = fragmentOffset(index);
  const uint16_t remaining = original_len - offset;
  return remaining > CHUNK_LEN ? CHUNK_LEN : remaining;
}

size_t buildFragment(uint8_t* dest, size_t dest_size,
                     const uint8_t* original, uint16_t original_len,
                     uint16_t fragment_id, uint8_t index, uint8_t flags) {
  const uint8_t count = fragmentCount(original_len);
  const uint16_t chunk_len = fragmentChunkLength(original_len, index);
  if (!dest || !original || fragment_id == 0 || count == 0 || chunk_len == 0 ||
      (flags & ~KNOWN_FLAGS) != 0 || dest_size < HEADER_LEN + chunk_len) return 0;

  dest[0] = FRAME_TYPE;
  memcpy(&dest[1], VERSION, sizeof(VERSION));
  dest[5] = flags;
  dest[6] = static_cast<uint8_t>(fragment_id);
  dest[7] = static_cast<uint8_t>(fragment_id >> 8);
  dest[8] = index;
  dest[9] = count;
  dest[10] = original[0];
  dest[11] = static_cast<uint8_t>(original_len);
  dest[12] = static_cast<uint8_t>(original_len >> 8);
  const uint16_t offset = fragmentOffset(index);
  dest[13] = static_cast<uint8_t>(offset);
  dest[14] = static_cast<uint8_t>(offset >> 8);
  memcpy(&dest[HEADER_LEN], &original[offset], chunk_len);
  return HEADER_LEN + chunk_len;
}

#endif  // WITH_COMPANION_FRAME_FRAGMENTS

}  // namespace companion_frames
