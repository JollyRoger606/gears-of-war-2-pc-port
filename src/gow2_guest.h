// gow2 - ReXGlue Recompiled Project
//
// Big-endian reads and writes of guest memory, addressed from the guest base pointer.
#pragma once

#include <cstdint>

namespace rex::glue::guest {

inline uint8_t LoadU8(const uint8_t* base, uint32_t addr) { return base[addr]; }

inline uint32_t LoadU32(const uint8_t* base, uint32_t addr) {
  const uint8_t* p = base + addr;
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

inline uint16_t LoadU16(const uint8_t* base, uint32_t addr) {
  const uint8_t* p = base + addr;
  return uint16_t((p[0] << 8) | p[1]);
}

inline void StoreU8(uint8_t* base, uint32_t addr, uint8_t v) { base[addr] = v; }

inline void StoreU32(uint8_t* base, uint32_t addr, uint32_t v) {
  uint8_t* p = base + addr;
  p[0] = uint8_t(v >> 24);
  p[1] = uint8_t(v >> 16);
  p[2] = uint8_t(v >> 8);
  p[3] = uint8_t(v);
}

}  // namespace rex::glue::guest
