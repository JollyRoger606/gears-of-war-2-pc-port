// gow2 - ReXGlue Recompiled Project
//
// Per-frame engine tick listener registry, run before UGameEngine::Tick.
#pragma once

#include <cstdint>

#include <rex/ppc.h>

namespace rex::glue {
// Listeners run on the game thread, once per frame, before UGameEngine::Tick. Rules:
// never modify ctx (copy it into a local PPCContext for guest calls);
// read settings once and return at once when off;
// never block or loop for long: the game waits for every listener.
using EngineTickListener = void (*)(const PPCContext& ctx, uint8_t* base);
void AddEngineTickListener(EngineTickListener listener);
void RunEngineTickListeners(const PPCContext& ctx, uint8_t* base);
}  // namespace rex::glue
