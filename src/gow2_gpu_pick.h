// gow2 - ReXGlue Recompiled Project
//
// Which GPU the game renders on. By default it is the one Windows treats as high performance
// (Settings > System > Display > Graphics, per app or system-wide), so an integrated GPU with a
// large shared-memory reservation no longer wins over the graphics card.
#pragma once

#include <string>

namespace rex::glue {

struct GpuChoice {
  int index = -1;     // DXGI adapter index for d3d12_adapter, -1 when nothing was chosen
  std::string name;
  std::string reason;
};

// Reads the gow2_gpu cvar: "auto", part of an adapter's name (e.g. "7800") or an adapter index.
GpuChoice ChooseRenderGpu();

}  // namespace rex::glue
