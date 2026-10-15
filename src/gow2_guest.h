// gow2 - ReXGlue Recompiled Project
//
// Big-endian reads and writes of guest memory, addressed from the guest base pointer.
#pragma once

#include <cstdint>
#include <vector>

namespace rex::glue::guest {

constexpr uint32_t kPhysicalHeap4K = 0xE0000000u;

// Must equal rex::memory::detail::PhysicalHostOffset (gow2_script_patch_hook.cpp checks it): where
// the host maps memory in 64 KiB or 16 KiB units the runtime puts the 0xE0000000 heap 0x1000 higher.
constexpr uint32_t PhysicalHostOffset([[maybe_unused]] uint32_t addr) {
#if defined(_WIN32) || (defined(__APPLE__) && defined(__aarch64__))
  return addr >= kPhysicalHeap4K ? 0x1000u : 0u;
#else
  return 0u;
#endif
}

inline const uint8_t* HostPtr(const uint8_t* base, uint32_t addr) {
  return base + addr + PhysicalHostOffset(addr);
}

inline uint8_t* HostPtr(uint8_t* base, uint32_t addr) {
  return base + addr + PhysicalHostOffset(addr);
}

inline uint8_t LoadU8(const uint8_t* base, uint32_t addr) { return *HostPtr(base, addr); }

inline uint16_t LoadU16(const uint8_t* base, uint32_t addr) {
  return uint16_t((LoadU8(base, addr) << 8) | LoadU8(base, addr + 1));
}

inline uint32_t LoadU32(const uint8_t* base, uint32_t addr) {
  return (uint32_t(LoadU8(base, addr)) << 24) | (uint32_t(LoadU8(base, addr + 1)) << 16) |
         (uint32_t(LoadU8(base, addr + 2)) << 8) | LoadU8(base, addr + 3);
}

inline void StoreU8(uint8_t* base, uint32_t addr, uint8_t v) { *HostPtr(base, addr) = v; }

inline void StoreU32(uint8_t* base, uint32_t addr, uint32_t v) {
  StoreU8(base, addr, uint8_t(v >> 24));
  StoreU8(base, addr + 1, uint8_t(v >> 16));
  StoreU8(base, addr + 2, uint8_t(v >> 8));
  StoreU8(base, addr + 3, uint8_t(v));
}

// Byte by byte, so a range that crosses into the 0xE0000000 heap still lands where the game sees it.
inline std::vector<uint8_t> ReadBytes(const uint8_t* base, uint32_t addr, uint32_t count) {
  std::vector<uint8_t> out(count);
  for (uint32_t i = 0; i < count; ++i) out[i] = LoadU8(base, addr + i);
  return out;
}

inline void WriteBytes(uint8_t* base, uint32_t addr, const std::vector<uint8_t>& bytes) {
  for (size_t i = 0; i < bytes.size(); ++i) StoreU8(base, addr + uint32_t(i), bytes[i]);
}

}  // namespace rex::glue::guest
