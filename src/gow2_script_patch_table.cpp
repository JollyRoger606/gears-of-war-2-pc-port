// gow2 - ReXGlue Recompiled Project
//
// The Stage 1 co-op rows of docs/COOP_N_PLAYERS.md section 4. Printed by
// `python -I tools/script_patch.py --cpp`; edit the rows there, not here. Check them against your
// own decompressed packages with `python -I tools/script_patch.py --check <folder>`.

#include "gow2_script_patches.h"

namespace rex::glue::script_patch {

namespace {

// return NumPlayers == 2  ->  return NumPlayers >= N
ScriptRow RowS1_1() {
  return {
      .id = "S1-1",
      .function = {"GearGame.GearGameSP_Base.AtCapacity", "Function"},
      .script_size = 17,
      .checks = {
          {0x0000, {0x04}},
          {0x0002, {0x01}},
          {0x0007, {0x2C}},
          {0x0009, {0x16, 0x04, 0x3A}},
          {0x0010, {0x53}},
      },
      .operands = {
          {0x0003, {"Engine.GameInfo.NumPlayers", "IntProperty"}},
          {0x000C, {"GearGame.GearGameSP_Base.AtCapacity.ReturnValue", "BoolProperty"}},
      },
      .byte_edits = {
          {0x0001, 0x9A, 0x99},
          {0x0008, 0x02, kPlayers},
      },
  };
}

// return GearPawn_COGGear  ->  Ben Carmine, else RedShirt, by DynamicLoadObject
ScriptRow RowS1_2() {
  return {
      .id = "S1-2",
      .function = {"GearGame.GearGameSP_Base.SuggestPawnClassFor", "Function"},
      .script_size = 145,
      .checks = {
          {0x0061, {0x04, 0x20}},
          {0x007E, {0x04, 0x20}},
          {0x0084, {0x04, 0x20}},
          {0x008A, {0x04, 0x3A}},
          {0x0090, {0x53}},
      },
      .operands = {
          {0x0063, {"GearGame.GearPawn_COGMarcus", "Class"}},
          {0x0080, {"GearGame.GearPawn_COGDom", "Class"}},
          {0x0086, {"GearGame.GearPawn_COGGear", "Class"}},
          {0x008C, {"GearGame.GearGameSP_Base.SuggestPawnClassFor.ReturnValue", "ClassProperty"}},
      },
      .grow = GrowEdit{
          .keep = 0x0084,
          .new_size = 279,
          .tail = {
              {.bytes = {0x0F, 0x00}},
              {.object = {"GearGame.GearGameSP_Base.SuggestPawnClassFor.ReturnValue", "ClassProperty"}},
              {.bytes = {0x13}},
              {.object = {"GearGame.GearPawn", "Class"}},
              {.bytes = {0x1C}},
              {.object = {"Core.Object.DynamicLoadObject", "Function"}},
              {.bytes = {0x1F}},
              {.bytes = AsciiZ("GearGameContent.GearPawn_COGBenjaminCarmine")},
              {.bytes = {0x20}},
              {.object = {"Core.Class", "Class"}},
              {.bytes = {0x27, 0x16, 0x07, 0x01, 0x10, 0x72, 0x00}},
              {.object = {"GearGame.GearGameSP_Base.SuggestPawnClassFor.ReturnValue", "ClassProperty"}},
              {.bytes = {0x2A, 0x16, 0x0F, 0x00}},
              {.object = {"GearGame.GearGameSP_Base.SuggestPawnClassFor.ReturnValue", "ClassProperty"}},
              {.bytes = {0x13}},
              {.object = {"GearGame.GearPawn", "Class"}},
              {.bytes = {0x1C}},
              {.object = {"Core.Object.DynamicLoadObject", "Function"}},
              {.bytes = {0x1F}},
              {.bytes = AsciiZ("GearGameContent.GearPawn_COGRedShirt")},
              {.bytes = {0x20}},
              {.object = {"Core.Class", "Class"}},
              {.bytes = {0x27, 0x16, 0x04, 0x3A}},
              {.object = {"GearGame.GearGameSP_Base.SuggestPawnClassFor.ReturnValue", "ClassProperty"}},
              {.bytes = {0x53}},
          },
      },
  };
}

// Coop.NumPublicConnections = 2  ->  N
ScriptRow RowS1_3() {
  return {
      .id = "S1-3",
      .function = {"GearGame.GearMenuGame.ChangeCoopInviteSetting", "Function"},
      .script_size = 178,
      .checks = {
          {0x0047, {0x0F, 0x19, 0x00}},
          {0x004E, {0x00, 0x05, 0x00, 0x04, 0x01}},
          {0x0057, {0x2C}},
          {0x0086, {0x0F, 0x01}},
          {0x008C, {0x92}},
      },
      .operands = {
          {0x004A, {"GearGame.GearMenuGame.ChangeCoopInviteSetting.Coop", "ObjectProperty"}},
          {0x0053, {"Engine.OnlineGameSettings.NumPublicConnections", "IntProperty"}},
          {0x0088, {"Engine.GameInfo.MaxPlayers", "IntProperty"}},
      },
      .byte_edits = {
          {0x0058, 0x02, kPlayers},
      },
  };
}

// Default__GearCoopGameSettings.NumPublicConnections = 2  ->  N
DefaultRow RowS1_4() {
  return {
      .id = "S1-4",
      .object = {"GearGame.Default__GearCoopGameSettings", "GearCoopGameSettings"},
      .offset = 132,
      .old_value = 2,
      .new_value = kPlayers,
  };
}

// MaxPlayers = 2  ->  N
ScriptRow RowS1_5() {
  return {
      .id = "S1-5",
      .function = {"GearGame.GearCampaignLobbyGame_Base.InitGame", "Function"},
      .script_size = 169,
      .checks = {
          {0x009E, {0x0F, 0x01}},
          {0x00A4, {0x2C}},
          {0x00A6, {0x04, 0x0B, 0x53}},
      },
      .operands = {
          {0x00A0, {"Engine.GameInfo.MaxPlayers", "IntProperty"}},
      },
      .byte_edits = {
          {0x00A5, 0x02, kPlayers},
      },
  };
}

// MaxPlayers = 2  ->  N
ScriptRow RowS1_6() {
  return {
      .id = "S1-6",
      .function = {"GearGame.GearGameSP_Base.InitGame", "Function"},
      .script_size = 202,
      .checks = {
          {0x00B4, {0x0F, 0x01}},
          {0x00BA, {0x2C}},
          {0x00BC, {0x0F, 0x01}},
          {0x00C2, {0x1D, 0x00, 0x00, 0x1B, 0x58, 0x04, 0x0B, 0x53}},
      },
      .operands = {
          {0x00B6, {"Engine.GameInfo.MaxPlayers", "IntProperty"}},
          {0x00BE, {"Engine.GameInfo.MaxDynamicBandwidth", "IntProperty"}},
      },
      .byte_edits = {
          {0x00BB, 0x02, kPlayers},
      },
  };
}

// Diagnostic: return GearPawn_COGGear  ->  return GearPawn_COGMarcus (players 3+ are Marcus clones)
ScriptRow RowD_1() {
  return {
      .id = "D-1",
      .function = {"GearGame.GearGameSP_Base.SuggestPawnClassFor", "Function"},
      .script_size = 145,
      .checks = {
          {0x0061, {0x04, 0x20}},
          {0x007E, {0x04, 0x20}},
          {0x0084, {0x04, 0x20}},
          {0x008A, {0x04, 0x3A}},
          {0x0090, {0x53}},
      },
      .operands = {
          {0x0063, {"GearGame.GearPawn_COGMarcus", "Class"}},
          {0x0080, {"GearGame.GearPawn_COGDom", "Class"}},
          {0x008C, {"GearGame.GearGameSP_Base.SuggestPawnClassFor.ReturnValue", "ClassProperty"}},
      },
      .operand_edits = {
          {0x0086, {"GearGame.GearPawn_COGGear", "Class"}, {"GearGame.GearPawn_COGMarcus", "Class"}},
      },
  };
}

}  // namespace

PatchGroup CoopStage1Group(bool marcus_clones) {
  return {
      .name = "coop",
      .scripts = {RowS1_1(), marcus_clones ? RowD_1() : RowS1_2(), RowS1_3(), RowS1_5(), RowS1_6()},
      .defaults = {RowS1_4()},
  };
}

}  // namespace rex::glue::script_patch
