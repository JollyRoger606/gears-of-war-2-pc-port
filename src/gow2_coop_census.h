// gow2 - ReXGlue Recompiled Project
//
// Co-op diagnostics: which pawn classes are loaded and which pawns exist (gow2_coop_census).
#pragma once

#include <cstdint>

namespace rex::glue {

// Starts the census thread once when gow2_coop_census is on; call after the script packages load.
void StartCoopCensus(const uint8_t* base);

}  // namespace rex::glue
