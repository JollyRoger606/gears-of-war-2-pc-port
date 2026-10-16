// gow2 - ReXGlue Recompiled Project
//
// Safe guest calls for dev commands: allocator, names, console, events.
#pragma once

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

#include <rex/ppc.h>

namespace rex::glue::guest_call {

constexpr uint32_t kGMalloc = 0x8310B12C;
constexpr uint32_t kMallocSlot = 4;
constexpr uint32_t kMallocAlign = 8;
constexpr uint32_t kAppFree = 0x82220050;
constexpr uint32_t kFNameInit = 0x82281710;
constexpr uint32_t kFindFunction = 0x82287B10;
constexpr uint32_t kConsoleCommandSlot = 272;
constexpr uint32_t kProcessEventSlot = 236;
constexpr uint32_t kMaxCommandChars = 400;
constexpr uint32_t kMaxResultChars = 512;
constexpr uint32_t kParmsBytes = 1024;
constexpr uint32_t kMaxNameChars = 128;

bool Invoke(const PPCContext& ctx, uint8_t* base, uint32_t function,
            std::initializer_list<uint64_t> gprs, uint32_t* r3_out = nullptr);
uint32_t VirtualFunction(const uint8_t* base, uint32_t object, uint32_t slot_offset);
uint32_t Malloc(const PPCContext& ctx, uint8_t* base, uint32_t size);
void Free(const PPCContext& ctx, uint8_t* base, uint32_t address);
bool IsPlainName(std::string_view text, size_t max_chars);
std::optional<uint64_t> MakeName(const PPCContext& ctx, uint8_t* base, std::string_view ascii,
                                 bool add);
std::optional<std::string> ConsoleCommand(const PPCContext& ctx, uint8_t* base, uint32_t pc,
                                          std::string_view ascii);
uint32_t FindFunction(const PPCContext& ctx, uint8_t* base, uint32_t object, uint64_t name);
bool ProcessEvent(const PPCContext& ctx, uint8_t* base, uint32_t object, uint32_t function);
bool GuardedRun(void (*fn)(void* arg), void* arg);

template <class F>
bool Guarded(F& f) {
  return GuardedRun([](void* p) { (*static_cast<F*>(p))(); }, &f);
}

}  // namespace rex::glue::guest_call
