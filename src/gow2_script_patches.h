// gow2 - ReXGlue Recompiled Project
//
// UnrealScript patches applied in guest memory, see docs/COOP_N_PLAYERS.md section 2. A group of
// rows edits loaded bytecode and default objects. Every row of a group is validated before any
// byte is written, and one failed check leaves the whole game retail.
//
// This part is portable and does not depend on the runtime: the hook that calls it lives in
// gow2_script_patch_hook.cpp, the rows in gow2_script_patch_table.cpp.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rex::glue::script_patch {

// An object found by its whole outer chain ("Package.Class[.State].Name") and the name of its
// class ("Function", "Class", "IntProperty", or the class of a default object).
struct ObjectRef {
  std::string_view path;
  std::string_view cls;
};

// In a byte value: the configured player count.
constexpr int kPlayers = -1;

// Bytes that must already be there (context and unchanged operator bytes).
struct BytesCheck {
  uint16_t offset;
  std::vector<uint8_t> bytes;
};

// A 4-byte object operand that must hold the address of this object.
struct OperandCheck {
  uint16_t offset;
  ObjectRef object;
};

// A same-size one-byte write. The byte must hold old_value, or new_value when already applied.
struct ByteEdit {
  uint16_t offset;
  uint8_t old_value;
  int new_value;  // 0..255 or kPlayers
};

// A 4-byte object operand swapped from one object to another.
struct OperandEdit {
  uint16_t offset;
  ObjectRef from;
  ObjectRef to;
};

// A piece of a grown script: literal bytes, or the address of an object when object.path is set.
struct TailPiece {
  std::vector<uint8_t> bytes;
  ObjectRef object;
};

// The script is replaced by its first `keep` bytes followed by the tail, new_size bytes in all.
struct GrowEdit {
  uint16_t keep;
  uint32_t new_size;
  std::vector<TailPiece> tail;
};

struct ScriptRow {
  std::string_view id;
  ObjectRef function;
  uint32_t script_size;
  std::vector<BytesCheck> checks;
  std::vector<OperandCheck> operands;
  std::vector<ByteEdit> byte_edits;
  std::vector<OperandEdit> operand_edits;
  std::optional<GrowEdit> grow;
};

// A big-endian int field of an object, usually a class default object.
struct DefaultRow {
  std::string_view id;
  ObjectRef object;
  uint32_t offset;
  uint32_t old_value;
  int new_value;  // or kPlayers
};

struct PatchGroup {
  std::string_view name;
  std::vector<ScriptRow> scripts;
  std::vector<DefaultRow> defaults;
};

// Guest addresses of the engine's name table (TArray<FNameEntry*>) and object table
// (TArray<UObject*>).
struct GuestTables {
  uint32_t names;
  uint32_t objects;
};

// Allocates guest memory with the game's own allocator; 0 on failure.
using GuestAlloc = std::function<uint32_t(uint32_t size)>;

struct GroupResult {
  bool applied = false;
  std::string failed_row;          // set when the group was skipped
  std::string reason;              // why it was skipped
  std::vector<std::string> lines;  // one per row when applied
  uint64_t set_hash = 0;           // GroupHash of what was applied
};

std::vector<uint8_t> AsciiZ(std::string_view text);

// The Stage 1 co-op group (rows S1-1..S1-6). With marcus_clones, row D-1 replaces S1-2.
PatchGroup CoopStage1Group(bool marcus_clones);

// Stable over runs and platforms: depends only on the rows and the player count.
uint64_t GroupHash(const PatchGroup& group, int players);

GroupResult ApplyGroup(uint8_t* base, const GuestTables& tables, const PatchGroup& group,
                       int players, const GuestAlloc& alloc);

}  // namespace rex::glue::script_patch
