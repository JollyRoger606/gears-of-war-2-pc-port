// gow2 - ReXGlue Recompiled Project
//
// The dev-only devcheats row of docs/TEST_TOOLING.md. Printed by
// `python -I tools/dev_cheats_patch.py --cpp`; edit the row there, not here. Check it against your
// own decompressed packages with `python -I tools/dev_cheats_patch.py --check <folder>`.

#include "gow2_dev_cheats.h"

namespace rex::glue::script_patch {

namespace {

// return NetMode == NM_Standalone  ->  return NetMode <= NM_ListenServer
ScriptRow RowDEV_1() {
  return {
      .id = "DEV-1",
      .function = {"Engine.GameInfo.AllowCheats", "Function"},
      .script_size = 31,
      .checks = {
          {0x0000, {0x04}},
          {0x0002, {0x38, 0x3A, 0x19, 0x01}},
          {0x000A, {0x00, 0x05, 0x00, 0x01, 0x01}},
          {0x0013, {0x38, 0x3A, 0x24}},
          {0x0017, {0x16, 0x04, 0x3A}},
          {0x001E, {0x53}},
      },
      .operands = {
          {0x0006, {"Engine.Actor.WorldInfo", "ObjectProperty"}},
          {0x000F, {"Engine.WorldInfo.NetMode", "ByteProperty"}},
          {0x001A, {"Engine.GameInfo.AllowCheats.ReturnValue", "BoolProperty"}},
      },
      .byte_edits = {
          {0x0001, 0x9A, 0x98},
          {0x0016, 0x00, 0x02},
      },
  };
}

}  // namespace

PatchGroup DevCheatsGroup() {
  return {
      .name = "devcheats",
      .scripts = {RowDEV_1()},
      .defaults = {},
  };
}

}  // namespace rex::glue::script_patch
