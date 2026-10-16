// gow2 - ReXGlue Recompiled Project
//
// Runs the registered per-frame listeners, then the real UGameEngine::Tick.
#include "gow2_engine_tick.h"

#include <vector>

#include <rex/hook.h>
#include <rex/ppc.h>

namespace rex::glue {
namespace {

std::vector<EngineTickListener>& Listeners() {
  // Function-local static, so registration from other files' static initializers works in any order.
  static std::vector<EngineTickListener> listeners;
  return listeners;
}

}  // namespace

void AddEngineTickListener(EngineTickListener listener) {
  if (listener == nullptr) return;
  Listeners().push_back(listener);
}

void RunEngineTickListeners(const PPCContext& ctx, uint8_t* base) {
  for (EngineTickListener listener : Listeners()) listener(ctx, base);
}

}  // namespace rex::glue

REX_EXTERN(__imp__sub_824A2EA0);

// UGameEngine::Tick(float): r3 = engine, f1 = delta; called once per frame from UGearEngine::Tick (sub_828A4180).
REX_HOOK_RAW(sub_824A2EA0) {
  rex::glue::RunEngineTickListeners(ctx, base);
  __imp__sub_824A2EA0(ctx, base);
}
