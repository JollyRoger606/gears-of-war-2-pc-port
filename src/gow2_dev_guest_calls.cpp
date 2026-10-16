// gow2 - ReXGlue Recompiled Project
//
// Fail-closed guest calls: allocator, FName, console command, event dispatch.
#include "gow2_dev_guest_calls.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <rex/runtime.h>
#include <rex/system/function_dispatcher.h>

#include "gow2_dev_commands.h"
#include "gow2_guest.h"

namespace rex::glue::guest_call {
namespace {

bool IsConsoleText(std::string_view text) {
  if (text.empty() || text.size() > kMaxCommandChars) return false;
  for (char c : text) {
    if (c < 0x20 || c > 0x7E) return false;
  }
  return true;
}

}  // namespace

bool Invoke(const PPCContext& ctx, uint8_t* base, uint32_t function,
            std::initializer_list<uint64_t> gprs, uint32_t* r3_out) {
  if (function == 0) return false;
  rex::Runtime* runtime = rex::Runtime::instance();
  rex::runtime::FunctionDispatcher* dispatcher = runtime ? runtime->function_dispatcher() : nullptr;
  if (dispatcher == nullptr) return false;
  PPCFunc* fn = dispatcher->GetFunction(function);
  if (fn == nullptr) return false;
  if (gprs.size() > 6) return false;
  PPCContext call = ctx;
  PPCRegister* regs[] = {&call.r3, &call.r4, &call.r5, &call.r6, &call.r7, &call.r8};
  size_t i = 0;
  for (uint64_t value : gprs) regs[i++]->u64 = value;
  fn(call, base);
  if (r3_out != nullptr) *r3_out = call.r3.u32;
  return true;
}

uint32_t VirtualFunction(const uint8_t* base, uint32_t object, uint32_t slot_offset) {
  if (object == 0) return 0;
  const uint32_t vtable = guest::LoadU32(base, object);
  if (vtable == 0) return 0;
  return guest::LoadU32(base, vtable + slot_offset);
}

uint32_t Malloc(const PPCContext& ctx, uint8_t* base, uint32_t size) {
  const uint32_t gmalloc = guest::LoadU32(base, kGMalloc);
  if (gmalloc == 0) return 0;
  const uint32_t fn = VirtualFunction(base, gmalloc, kMallocSlot);
  uint32_t out = 0;
  if (!Invoke(ctx, base, fn, {gmalloc, size, kMallocAlign}, &out)) return 0;
  return out;
}

void Free(const PPCContext& ctx, uint8_t* base, uint32_t address) {
  if (address == 0) return;
  Invoke(ctx, base, kAppFree, {address});
}

bool IsPlainName(std::string_view text, size_t max_chars) {
  if (text.empty() || text.size() > max_chars) return false;
  for (char c : text) {
    const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                       (c >= '0' && c <= '9') || c == '_';
    if (!plain) return false;
  }
  return true;
}

std::optional<uint64_t> MakeName(const PPCContext& ctx, uint8_t* base, std::string_view ascii,
                                 bool add) {
  if (!IsPlainName(ascii, kMaxNameChars)) return std::nullopt;
  const uint32_t n = uint32_t(ascii.size());
  const uint32_t block = Malloc(ctx, base, 8 + 2 * (n + 1));
  if (block == 0) return std::nullopt;
  guest::StoreU32(base, block, 0);
  guest::StoreU32(base, block + 4, 0);
  guest::WriteBytes(base, block + 8, dev_commands::EncodeTcharZ(ascii));
  const uint64_t add_flag = add ? 1 : 0;
  const bool ok = Invoke(ctx, base, kFNameInit, {block, block + 8, 0, add_flag, 1});
  const uint32_t index = guest::LoadU32(base, block);
  const uint32_t number = guest::LoadU32(base, block + 4);
  Free(ctx, base, block);
  if (!ok || index == 0) return std::nullopt;
  return dev_commands::PackName(index, number);
}

std::optional<std::string> ConsoleCommand(const PPCContext& ctx, uint8_t* base, uint32_t pc,
                                          std::string_view ascii) {
  if (!IsConsoleText(ascii)) return std::nullopt;
  const uint32_t fn = VirtualFunction(base, pc, kConsoleCommandSlot);
  if (fn == 0) return std::nullopt;
  const uint32_t len = uint32_t(ascii.size());
  const uint32_t block = Malloc(ctx, base, 24 + 2 * (len + 1));
  if (block == 0) return std::nullopt;
  guest::StoreU32(base, block, block + 24);
  guest::StoreU32(base, block + 4, len + 1);
  guest::StoreU32(base, block + 8, len + 1);
  guest::StoreU32(base, block + 12, 0);
  guest::StoreU32(base, block + 16, 0);
  guest::StoreU32(base, block + 20, 0);
  guest::WriteBytes(base, block + 24, dev_commands::EncodeTcharZ(ascii));
  // r3 = hidden return FString*, r4 = this, r5 = const FString&, r6 = bWriteToLog.
  const bool ok = Invoke(ctx, base, fn, {block + 12, pc, block, 0});
  const std::string out =
      dev_commands::DecodeTchars(base, guest::LoadU32(base, block + 12),
                                 int32_t(guest::LoadU32(base, block + 16)), kMaxResultChars);
  Free(ctx, base, guest::LoadU32(base, block + 12));
  Free(ctx, base, block);
  if (!ok) return std::nullopt;
  return out;
}

uint32_t FindFunction(const PPCContext& ctx, uint8_t* base, uint32_t object, uint64_t name) {
  if (object == 0) return 0;
  uint32_t out = 0;
  if (!Invoke(ctx, base, kFindFunction, {object, name, 0}, &out)) return 0;
  return out;
}

bool ProcessEventWithParms(const PPCContext& ctx, uint8_t* base, uint32_t object,
                           uint32_t function, const std::vector<uint8_t>& parms_in,
                           std::vector<uint8_t>* parms_out, uint32_t out_bytes) {
  if (object == 0 || function == 0) return false;
  const uint32_t fn = VirtualFunction(base, object, kProcessEventSlot);
  if (fn == 0) return false;
  const uint32_t parms = Malloc(ctx, base, kParmsBytes);
  if (parms == 0) return false;
  for (uint32_t i = 0; i < kParmsBytes; ++i) {
    const uint8_t v = i < parms_in.size() ? parms_in[i] : 0;
    guest::StoreU8(base, parms + i, v);
  }
  const bool ok = Invoke(ctx, base, fn, {object, function, parms, 0});
  if (parms_out != nullptr) {
    const uint32_t want = out_bytes < kParmsBytes ? out_bytes : kParmsBytes;
    *parms_out = guest::ReadBytes(base, parms, want);
  }
  Free(ctx, base, parms);
  return ok;
}

bool ProcessEvent(const PPCContext& ctx, uint8_t* base, uint32_t object, uint32_t function) {
  return ProcessEventWithParms(ctx, base, object, function, {}, nullptr, 0);
}

bool GuardedRun(void (*fn)(void* arg), void* arg) {
#ifdef _WIN32
  __try {
    fn(arg);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
#else
  fn(arg);
  return true;
#endif
}

}  // namespace rex::glue::guest_call
