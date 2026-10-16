// gow2 - ReXGlue Recompiled Project
//
// Validation and application of script patch groups, see gow2_script_patches.h.

#include "gow2_script_patches.h"

#include <algorithm>
#include <unordered_map>
#include <utility>

#include "gow2_guest.h"

namespace rex::glue::script_patch {

namespace {

// UE3 guest layout, docs/COOP_N_PLAYERS.md section 2.
constexpr uint32_t kArrayData = 0;
constexpr uint32_t kArrayNum = 4;
constexpr uint32_t kArrayMax = 8;
constexpr uint32_t kObjOuter = 40;
constexpr uint32_t kObjNameIndex = 44;
constexpr uint32_t kObjNameNumber = 48;
constexpr uint32_t kObjClass = 52;
constexpr uint32_t kStructScript = 84;
constexpr uint32_t kNameEntryFlags = 0;
constexpr uint32_t kNameEntryText = 8;
constexpr uint32_t kNameWideBit = 1;
constexpr uint32_t kMaxTableCount = 1u << 24;

struct GuestArray {
  uint32_t data = 0;
  uint32_t num = 0;
  uint32_t max = 0;
};

GuestArray ReadArray(const uint8_t* base, uint32_t addr) {
  return {guest::LoadU32(base, addr + kArrayData), guest::LoadU32(base, addr + kArrayNum),
          guest::LoadU32(base, addr + kArrayMax)};
}

bool PlausibleTable(const GuestArray& a) {
  return a.data != 0 && a.num != 0 && a.num <= kMaxTableCount && a.max >= a.num;
}

uint32_t FoldAscii(uint32_t c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

std::vector<std::string_view> SplitPath(std::string_view path) {
  std::vector<std::string_view> parts;
  size_t start = 0;
  while (start <= path.size()) {
    const size_t dot = path.find('.', start);
    const size_t end = dot == std::string_view::npos ? path.size() : dot;
    parts.push_back(path.substr(start, end - start));
    if (dot == std::string_view::npos) break;
    start = dot + 1;
  }
  return parts;
}

std::string Hex(uint32_t v, int digits) {
  static const char* kDigits = "0123456789ABCDEF";
  std::string out(size_t(digits), '0');
  for (int i = digits - 1; i >= 0; --i, v >>= 4) out[size_t(i)] = kDigits[v & 15];
  return out;
}

std::string At(uint32_t offset) { return "+0x" + Hex(offset, 4); }

std::string Describe(const ObjectRef& ref) {
  return std::string(ref.path) + " (" + std::string(ref.cls) + ")";
}

// Names and objects of the running engine, looked up by text with case folded as UE3 does.
class GuestObjects {
 public:
  GuestObjects(const uint8_t* base, const GuestTables& tables)
      : base_(base),
        names_(ReadArray(base, tables.names)),
        objects_(ReadArray(base, tables.objects)) {}

  bool Readable() const { return PlausibleTable(names_) && PlausibleTable(objects_); }

  std::vector<uint32_t> Find(const ObjectRef& ref) {
    std::vector<uint32_t> hits;
    std::vector<uint32_t> path;
    for (std::string_view part : SplitPath(ref.path)) {
      const std::optional<uint32_t> index = NameIndex(part);
      if (!index) return hits;
      path.push_back(*index);
    }
    const std::optional<uint32_t> cls = NameIndex(ref.cls);
    if (!cls || path.empty()) return hits;
    for (uint32_t i = 0; i < objects_.num; ++i) {
      const uint32_t object = guest::LoadU32(base_, objects_.data + 4 * i);
      if (object != 0 && ClassIs(object, *cls) && PathIs(object, path)) hits.push_back(object);
    }
    return hits;
  }

 private:
  bool NameEntryIs(uint32_t entry, std::string_view text) const {
    const bool wide = guest::LoadU32(base_, entry + kNameEntryFlags) & kNameWideBit;
    const uint32_t at = entry + kNameEntryText;
    const uint32_t width = wide ? 2 : 1;
    for (size_t i = 0; i <= text.size(); ++i) {
      const uint32_t addr = at + uint32_t(i) * width;
      const uint32_t c = wide ? guest::LoadU16(base_, addr) : guest::LoadU8(base_, addr);
      if (i == text.size()) return c == 0;
      if (c == 0 || FoldAscii(c) != FoldAscii(uint8_t(text[i]))) return false;
    }
    return false;
  }

  // The single name table index holding this text, or none when it is missing or ambiguous.
  std::optional<uint32_t> NameIndex(std::string_view text) {
    const std::string key(text);
    if (auto it = cache_.find(key); it != cache_.end()) return it->second;
    std::optional<uint32_t> found;
    int matches = 0;
    for (uint32_t i = 0; i < names_.num && matches < 2; ++i) {
      const uint32_t entry = guest::LoadU32(base_, names_.data + 4 * i);
      if (entry != 0 && NameEntryIs(entry, text)) {
        found = i;
        ++matches;
      }
    }
    if (matches != 1) found.reset();
    cache_.emplace(key, found);
    return found;
  }

  bool NameIs(uint32_t object, uint32_t index) const {
    return guest::LoadU32(base_, object + kObjNameIndex) == index &&
           guest::LoadU32(base_, object + kObjNameNumber) == 0;
  }

  bool ClassIs(uint32_t object, uint32_t cls_name) const {
    const uint32_t cls = guest::LoadU32(base_, object + kObjClass);
    return cls != 0 && NameIs(cls, cls_name);
  }

  // The object's outer chain spells the path exactly and ends at the top level.
  bool PathIs(uint32_t object, const std::vector<uint32_t>& path) const {
    uint32_t current = object;
    for (size_t i = path.size(); i-- > 0;) {
      if (current == 0 || !NameIs(current, path[i])) return false;
      current = guest::LoadU32(base_, current + kObjOuter);
    }
    return current == 0;
  }

  const uint8_t* base_;
  GuestArray names_;
  GuestArray objects_;
  std::unordered_map<std::string, std::optional<uint32_t>> cache_;
};

struct Write {
  uint32_t addr;
  std::vector<uint8_t> bytes;
};

struct ScriptSwap {
  uint32_t function;
  uint32_t data;
  std::vector<uint8_t> bytes;
};

std::vector<uint8_t> BigEndian(uint32_t v) {
  return {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)};
}

// Checks every row of a group and records the writes; nothing touches game data until Commit.
class GroupPlanner {
 public:
  GroupPlanner(uint8_t* base, GuestObjects& objects, int players, const GuestAlloc& alloc)
      : base_(base), objects_(objects), players_(players), alloc_(alloc) {}

  bool PlanScript(const ScriptRow& row) {
    current_id_ = row.id;
    uint32_t function = 0;
    if (!ResolveOne(row.function, &function) || !LoadScript(row, function)) return false;
    const size_t writes_before = writes_.size();
    if (!CheckBytes(row) || !CheckOperands(row) || !CheckEditsInKeptPrefix(row) ||
        !PlanByteEdits(row) || !PlanOperandEdits(row))
      return false;
    const size_t edits = writes_.size() - writes_before;
    const size_t swaps_before = swaps_.size();
    if (!PlanGrow(row, function, writes_before)) return false;
    const bool grew = swaps_.size() > swaps_before;
    lines_.push_back(RowLine(row.id, row.function, edits, grew ? row.grow->new_size : 0));
    return true;
  }

  bool PlanDefault(const DefaultRow& row) {
    current_id_ = row.id;
    uint32_t object = 0;
    if (!ResolveOne(row.object, &object)) return false;
    uint32_t value = 0;
    if (!ResolveValue(row.new_value, 0xFFFFFFFFu, &value)) return false;
    const uint32_t addr = object + row.offset;
    const uint32_t found = guest::LoadU32(base_, addr);
    size_t writes = 0;
    if (found == row.old_value) {
      writes_.push_back({addr, BigEndian(value)});
      writes = 1;
    } else if (found != value) {
      return Fail(At(row.offset) + " holds " + std::to_string(found) + ", expected " +
                  std::to_string(row.old_value));
    }
    lines_.push_back(RowLine(row.id, row.object, writes, 0));
    return true;
  }

  bool PlanObjectField(const ObjectFieldRow& row) {
    current_id_ = row.id;
    uint32_t object = 0;
    uint32_t value = 0;
    if (!ResolveOne(row.object, &object) || !ResolveOne(row.value, &value)) return false;
    for (const FieldAnchor& anchor : row.anchors)
      if (!CheckAnchor(anchor)) return false;
    const uint32_t addr = object + row.offset;
    const uint32_t found = guest::LoadU32(base_, addr);
    size_t writes = 0;
    if (found == 0) {
      writes_.push_back({addr, BigEndian(value)});
      writes = 1;
    } else if (found != value) {
      return Fail(At(row.offset) + " points at 0x" + Hex(found, 8) + ", expected None or " +
                  Describe(row.value));
    }
    lines_.push_back(RowLine(row.id, row.object, writes, 0));
    return true;
  }

  void Commit() {
    for (const Write& w : writes_) guest::WriteBytes(base_, w.addr, w.bytes);
    for (const ScriptSwap& s : swaps_) {
      guest::WriteBytes(base_, s.data, s.bytes);
      const uint32_t array = s.function + kStructScript;
      const uint32_t size = uint32_t(s.bytes.size());
      guest::StoreU32(base_, array + kArrayData, s.data);
      guest::StoreU32(base_, array + kArrayNum, size);
      guest::StoreU32(base_, array + kArrayMax, size);
    }
  }

  const std::string& failed_row() const { return failed_row_; }
  const std::string& reason() const { return reason_; }
  std::vector<std::string>& lines() { return lines_; }

 private:
  bool Fail(std::string why) {
    failed_row_ = std::string(current_id_);
    reason_ = std::move(why);
    return false;
  }

  bool LoadScript(const ScriptRow& row, uint32_t function) {
    script_ = ReadArray(base_, function + kStructScript);
    grown_ = row.grow && script_.num == row.grow->new_size;
    if (!grown_ && script_.num != row.script_size)
      return Fail("Script.Num is " + std::to_string(script_.num) + ", expected " +
                  std::to_string(row.script_size));
    if (script_.data == 0) return Fail("no script data");
    scope_ = grown_ ? row.grow->keep : script_.num;
    return true;
  }

  bool ResolveOne(const ObjectRef& ref, uint32_t* out) {
    const std::vector<uint32_t> hits = objects_.Find(ref);
    if (hits.size() != 1)
      return Fail(Describe(ref) + " found " + std::to_string(hits.size()) + " times");
    *out = hits[0];
    return true;
  }

  bool ResolveValue(int value, uint32_t limit, uint32_t* out) {
    const int v = value == kPlayers ? players_ : value;
    if (v < 0 || uint32_t(v) > limit) return Fail("value " + std::to_string(v) + " out of range");
    *out = uint32_t(v);
    return true;
  }

  bool CheckAnchor(const FieldAnchor& anchor) {
    uint32_t object = 0;
    uint32_t value = 0;
    if (!ResolveOne(anchor.object, &object) || !ResolveOne(anchor.value, &value)) return false;
    const uint32_t found = guest::LoadU32(base_, object + anchor.offset);
    if (found == value) return true;
    return Fail("anchor " + Describe(anchor.object) + " " + At(anchor.offset) + " points at 0x" +
                Hex(found, 8) + ", expected " + Describe(anchor.value));
  }

  // An entry past the kept prefix of an already grown script described the old tail: skip it.
  bool InScope(uint32_t offset, uint32_t size, bool* skip) {
    *skip = grown_ && offset + size > scope_;
    if (*skip || offset + size <= scope_) return true;
    return Fail(At(offset) + " is outside the script");
  }

  bool CheckBytes(const ScriptRow& row) {
    for (const BytesCheck& check : row.checks) {
      bool skip = false;
      if (!InScope(check.offset, uint32_t(check.bytes.size()), &skip)) return false;
      if (skip) continue;
      for (size_t i = 0; i < check.bytes.size(); ++i) {
        const uint32_t offset = check.offset + uint32_t(i);
        const uint8_t found = guest::LoadU8(base_, script_.data + offset);
        if (found != check.bytes[i])
          return Fail(At(offset) + " expected " + Hex(check.bytes[i], 2) + ", found " +
                      Hex(found, 2));
      }
    }
    return true;
  }

  bool CheckOperands(const ScriptRow& row) {
    for (const OperandCheck& operand : row.operands) {
      bool skip = false;
      if (!InScope(operand.offset, 4, &skip)) return false;
      if (skip) continue;
      uint32_t expected = 0;
      if (!ResolveOne(operand.object, &expected)) return false;
      const uint32_t found = guest::LoadU32(base_, script_.data + operand.offset);
      if (found != expected)
        return Fail(At(operand.offset) + " points at 0x" + Hex(found, 8) + ", expected " +
                    Describe(operand.object) + " at 0x" + Hex(expected, 8));
    }
    return true;
  }

  bool InKeptPrefix(uint32_t offset, uint32_t size, uint32_t keep) {
    if (offset + size <= keep) return true;
    return Fail(At(offset) + " is past the kept prefix " + At(keep));
  }

  // A grow keeps only the prefix, so an edit after it would be dropped without a word.
  bool CheckEditsInKeptPrefix(const ScriptRow& row) {
    if (!row.grow) return true;
    for (const ByteEdit& edit : row.byte_edits)
      if (!InKeptPrefix(edit.offset, 1, row.grow->keep)) return false;
    for (const OperandEdit& edit : row.operand_edits)
      if (!InKeptPrefix(edit.offset, 4, row.grow->keep)) return false;
    return true;
  }

  bool PlanByteEdits(const ScriptRow& row) {
    for (const ByteEdit& edit : row.byte_edits) {
      bool skip = false;
      if (!InScope(edit.offset, 1, &skip)) return false;
      if (skip) continue;
      uint32_t value = 0;
      if (!ResolveValue(edit.new_value, 0xFF, &value)) return false;
      const uint32_t addr = script_.data + edit.offset;
      const uint8_t found = guest::LoadU8(base_, addr);
      if (found == edit.old_value) {
        writes_.push_back({addr, {uint8_t(value)}});
      } else if (found != value) {
        return Fail(At(edit.offset) + " expected " + Hex(edit.old_value, 2) + " or " +
                    Hex(value, 2) + ", found " + Hex(found, 2));
      }
    }
    return true;
  }

  bool PlanOperandEdits(const ScriptRow& row) {
    for (const OperandEdit& edit : row.operand_edits) {
      bool skip = false;
      if (!InScope(edit.offset, 4, &skip)) return false;
      if (skip) continue;
      uint32_t from = 0;
      uint32_t to = 0;
      if (!ResolveOne(edit.from, &from) || !ResolveOne(edit.to, &to)) return false;
      const uint32_t addr = script_.data + edit.offset;
      const uint32_t found = guest::LoadU32(base_, addr);
      if (found == from) {
        writes_.push_back({addr, BigEndian(to)});
      } else if (found != to) {
        return Fail(At(edit.offset) + " points at 0x" + Hex(found, 8) + ", expected " +
                    Describe(edit.from));
      }
    }
    return true;
  }

  bool AssembleTail(const GrowEdit& grow, std::vector<uint8_t>* tail) {
    for (const TailPiece& piece : grow.tail) {
      if (piece.object.path.empty()) {
        tail->insert(tail->end(), piece.bytes.begin(), piece.bytes.end());
        continue;
      }
      uint32_t address = 0;
      if (!ResolveOne(piece.object, &address)) return false;
      const std::vector<uint8_t> operand = BigEndian(address);
      tail->insert(tail->end(), operand.begin(), operand.end());
    }
    if (grow.keep + tail->size() != grow.new_size)
      return Fail("grown script would be " + std::to_string(grow.keep + tail->size()) +
                  " bytes, expected " + std::to_string(grow.new_size));
    return true;
  }

  bool TailAlreadyThere(const GrowEdit& grow, const std::vector<uint8_t>& tail) const {
    return guest::ReadBytes(base_, script_.data + grow.keep, uint32_t(tail.size())) == tail;
  }

  // The row's edits go into the grown copy instead of the old buffer, which stays as loaded.
  void MoveRowWritesInto(std::vector<uint8_t>* prefix, size_t row_writes_begin) {
    for (size_t i = row_writes_begin; i < writes_.size(); ++i) {
      const Write& w = writes_[i];
      std::copy(w.bytes.begin(), w.bytes.end(), prefix->begin() + (w.addr - script_.data));
    }
    writes_.resize(row_writes_begin);
  }

  bool AllocateGrown(uint32_t size, uint32_t* data) {
    // Allocated before the group is known to pass; a later failure only leaks this block.
    *data = alloc_(size);
    if (*data == 0) return Fail("guest allocation of " + std::to_string(size) + " bytes failed");
    if (uint64_t(*data) + size > (uint64_t(1) << 32))
      return Fail("guest allocation returned 0x" + Hex(*data, 8) + " for " + std::to_string(size) +
                  " bytes");
    return true;
  }

  bool PlanGrow(const ScriptRow& row, uint32_t function, size_t row_writes_begin) {
    if (!row.grow) return true;
    const GrowEdit& grow = *row.grow;
    std::vector<uint8_t> tail;
    if (!AssembleTail(grow, &tail)) return false;
    if (grown_) {
      if (TailAlreadyThere(grow, tail)) return true;
      return Fail("Script.Num is already " + std::to_string(script_.num) + " but the tail differs");
    }
    if (script_.max != script_.num)
      return Fail("Script.Max is " + std::to_string(script_.max) + ", Num " +
                  std::to_string(script_.num));
    std::vector<uint8_t> script = guest::ReadBytes(base_, script_.data, grow.keep);
    MoveRowWritesInto(&script, row_writes_begin);
    script.insert(script.end(), tail.begin(), tail.end());
    uint32_t data = 0;
    if (!AllocateGrown(grow.new_size, &data)) return false;
    swaps_.push_back({function, data, std::move(script)});
    return true;
  }

  static std::string GrownText(uint32_t size, size_t writes) {
    std::string text = "script grown to " + std::to_string(size) + " bytes";
    if (writes > 0) text += ", with " + std::to_string(writes) + " write(s)";
    return text;
  }

  static std::string RowLine(std::string_view id, const ObjectRef& target, size_t writes,
                             uint32_t grown_size) {
    std::string line = std::string(id) + " " + std::string(target.path) + ": ";
    if (grown_size > 0) return line + GrownText(grown_size, writes);
    if (writes == 0) return line + "already applied";
    return line + std::to_string(writes) + " write(s)";
  }

  uint8_t* base_;
  GuestObjects& objects_;
  int players_;
  const GuestAlloc& alloc_;
  std::string_view current_id_;
  GuestArray script_;
  bool grown_ = false;
  uint32_t scope_ = 0;
  std::vector<Write> writes_;
  std::vector<ScriptSwap> swaps_;
  std::vector<std::string> lines_;
  std::string failed_row_;
  std::string reason_;
};

constexpr uint64_t kFnvBasis = 1469598103934665603ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

void HashBytes(uint64_t* h, const uint8_t* p, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    *h ^= p[i];
    *h *= kFnvPrime;
  }
}

void HashText(uint64_t* h, std::string_view s) {
  HashBytes(h, reinterpret_cast<const uint8_t*>(s.data()), s.size());
  const uint8_t separator = 0;
  HashBytes(h, &separator, 1);
}

void HashNumber(uint64_t* h, int64_t v) { HashText(h, std::to_string(v)); }

void HashRef(uint64_t* h, const ObjectRef& ref) {
  HashText(h, ref.path);
  HashText(h, ref.cls);
}

int64_t ValueOf(int value, int players) { return value == kPlayers ? players : value; }

void HashScriptRow(uint64_t* h, const ScriptRow& row, int players) {
  HashText(h, row.id);
  HashRef(h, row.function);
  HashNumber(h, row.script_size);
  for (const BytesCheck& c : row.checks) {
    HashNumber(h, c.offset);
    HashBytes(h, c.bytes.data(), c.bytes.size());
  }
  for (const OperandCheck& o : row.operands) {
    HashNumber(h, o.offset);
    HashRef(h, o.object);
  }
  for (const ByteEdit& e : row.byte_edits) {
    HashNumber(h, e.offset);
    HashNumber(h, e.old_value);
    HashNumber(h, ValueOf(e.new_value, players));
  }
  for (const OperandEdit& e : row.operand_edits) {
    HashNumber(h, e.offset);
    HashRef(h, e.from);
    HashRef(h, e.to);
  }
  if (!row.grow) return;
  HashNumber(h, row.grow->keep);
  HashNumber(h, row.grow->new_size);
  for (const TailPiece& p : row.grow->tail) {
    HashBytes(h, p.bytes.data(), p.bytes.size());
    HashRef(h, p.object);
  }
}

void HashDefaultRow(uint64_t* h, const DefaultRow& row, int players) {
  HashText(h, row.id);
  HashRef(h, row.object);
  HashNumber(h, row.offset);
  HashNumber(h, row.old_value);
  HashNumber(h, ValueOf(row.new_value, players));
}

void HashObjectFieldRow(uint64_t* h, const ObjectFieldRow& row) {
  HashText(h, row.id);
  HashRef(h, row.object);
  HashNumber(h, row.offset);
  HashRef(h, row.value);
  for (const FieldAnchor& a : row.anchors) {
    HashRef(h, a.object);
    HashNumber(h, a.offset);
    HashRef(h, a.value);
  }
}

GroupResult Skipped(std::string row, std::string reason) {
  GroupResult result;
  result.failed_row = std::move(row);
  result.reason = std::move(reason);
  return result;
}

}  // namespace

std::vector<uint8_t> AsciiZ(std::string_view text) {
  std::vector<uint8_t> out(text.begin(), text.end());
  out.push_back(0);
  return out;
}

uint64_t GroupHash(const PatchGroup& group, int players) {
  uint64_t h = kFnvBasis;
  HashText(&h, group.name);
  HashNumber(&h, players);
  for (const ScriptRow& row : group.scripts) HashScriptRow(&h, row, players);
  for (const DefaultRow& row : group.defaults) HashDefaultRow(&h, row, players);
  for (const ObjectFieldRow& row : group.object_fields) HashObjectFieldRow(&h, row);
  return h;
}

GroupResult ApplyGroup(uint8_t* base, const GuestTables& tables, const PatchGroup& group,
                       int players, const GuestAlloc& alloc) {
  GuestObjects objects(base, tables);
  if (!objects.Readable()) return Skipped("-", "the name or object table is unreadable");
  GroupPlanner planner(base, objects, players, alloc);
  for (const ScriptRow& row : group.scripts)
    if (!planner.PlanScript(row)) return Skipped(planner.failed_row(), planner.reason());
  for (const DefaultRow& row : group.defaults)
    if (!planner.PlanDefault(row)) return Skipped(planner.failed_row(), planner.reason());
  for (const ObjectFieldRow& row : group.object_fields)
    if (!planner.PlanObjectField(row)) return Skipped(planner.failed_row(), planner.reason());
  planner.Commit();
  GroupResult result;
  result.applied = true;
  result.lines = std::move(planner.lines());
  result.set_hash = GroupHash(group, players);
  return result;
}

}  // namespace rex::glue::script_patch
