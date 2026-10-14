// Adapted from gears-judgement-recomp (BSD-3-Clause).
// gow2 - ReXGlue Recompiled Project
//
// Host switches for the ported xenia-canary game patches
// (tools/patch_game_patches.py). The generated code asks Gow2GamePatch(id)
// at each patched instruction, so every patch is a live cvar. Patched
// lines in the generated code carry the marker "GOW2 GAMEPATCH <id>".

#include <rex/cvar.h>

REXCVAR_DEFINE_BOOL(gow2_black_shading_fix, true, "GameFixes",
                    "Black Shading Fix: effectively turns off MSAA (GUseTilingMode=FALSE), "
                    "solving the dynamic lighting artifacts caused by MSAA resolve imperfections.");
REXCVAR_DEFINE_BOOL(gow2_texture_streaming_fix, true, "GameFixes",
                    "Texture Streaming Fix: PoolSize=196 and RHIGetTextureMemoryStats=FALSE "
                    "(locks FudgeFactor=1.0), minimizing texture pop-in in heavy scenes.");
REXCVAR_DEFINE_BOOL(gow2_aniso16, true, "GameFixes",
                    "16x Anisotropic Filtering.");

extern "C" int Gow2GamePatch(int id) {
  switch (id) {
    case 1: return REXCVAR_GET(gow2_black_shading_fix);
    case 2: return REXCVAR_GET(gow2_texture_streaming_fix);
    case 3: return REXCVAR_GET(gow2_aniso16);
    default: return 0;
  }
}
