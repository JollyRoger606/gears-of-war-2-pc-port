// gow2 - ReXGlue Recompiled Project
//
// Co-op diagnostics (gow2_coop_census): a host thread reads the engine's object table every few
// seconds and logs, when they change, the COG pawn classes that are loaded (with their outer path,
// which is what DynamicLoadObject must spell), the pawns that exist and each player controller's
// DefaultPawnClass. Read-only; never runs guest code.

#include "gow2_coop_census.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "gow2_guest.h"

REXCVAR_DEFINE_BOOL(gow2_coop_census, false, "Coop",
                    "Debug: log the loaded GearPawn classes (with their package path) and the "
                    "pawns in the world whenever they change, every few seconds");

namespace rex::glue {
namespace {

constexpr uint32_t kGNames = 0x83136834;
constexpr uint32_t kGObjObjects = 0x83136860;
constexpr uint32_t kObjOuter = 40;
constexpr uint32_t kObjNameIndex = 44;
constexpr uint32_t kObjNameNumber = 48;
constexpr uint32_t kObjClass = 52;
// GearPC.DefaultPawnClass (layout of GearGame.GearPC, out/re-mouse/layout_GearPC.txt).
constexpr uint32_t kGearPcDefaultPawnClass = 1784;
constexpr uint32_t kNameEntryText = 8;
constexpr uint32_t kNameWideBit = 1;
constexpr uint32_t kMaxObjects = 1u << 22;
constexpr uint32_t kMaxNameLength = 128;
constexpr int kMaxOuterDepth = 8;
constexpr auto kPeriod = std::chrono::seconds(4);

struct Census {
  std::vector<std::string> pawn_classes;  // "Outer.Path.Name" of GearPawn_COG* classes
  std::map<std::string, int> pawns;       // class name -> count of instances
  std::map<std::string, std::string> pcs; // GearPC instance -> its DefaultPawnClass
  bool operator==(const Census&) const = default;
};

class Reader {
 public:
  explicit Reader(const uint8_t* base) : base_(base) {}

  std::string NameText(uint32_t index) const {
    const uint32_t names = guest::LoadU32(base_, kGNames);
    const uint32_t count = guest::LoadU32(base_, kGNames + 4);
    if (names == 0 || index >= count) return "?";
    const uint32_t entry = guest::LoadU32(base_, names + 4 * index);
    if (entry == 0) return "?";
    const bool wide = guest::LoadU32(base_, entry) & kNameWideBit;
    std::string out;
    for (uint32_t i = 0; i < kMaxNameLength; ++i) {
      const uint32_t at = entry + kNameEntryText + i * (wide ? 2 : 1);
      const uint32_t c = wide ? guest::LoadU16(base_, at) : guest::LoadU8(base_, at);
      if (c == 0) break;
      out.push_back(c < 128 ? char(c) : '?');
    }
    return out;
  }

  std::string ObjectName(uint32_t object) const {
    std::string name = NameText(guest::LoadU32(base_, object + kObjNameIndex));
    const uint32_t number = guest::LoadU32(base_, object + kObjNameNumber);
    if (number != 0) name += "_" + std::to_string(number - 1);
    return name;
  }

  std::string ObjectPath(uint32_t object) const {
    std::string path = ObjectName(object);
    uint32_t outer = guest::LoadU32(base_, object + kObjOuter);
    for (int depth = 0; outer != 0 && depth < kMaxOuterDepth; ++depth) {
      path = ObjectName(outer) + "." + path;
      outer = guest::LoadU32(base_, outer + kObjOuter);
    }
    return path;
  }

  std::string ClassName(uint32_t object) const {
    const uint32_t cls = guest::LoadU32(base_, object + kObjClass);
    return cls == 0 ? std::string("?") : ObjectName(cls);
  }

 private:
  const uint8_t* base_;
};

bool IsPawnClassName(const std::string& name) { return name.rfind("GearPawn_", 0) == 0; }

// Only the COG classes: the Locust ones are many and say nothing about the co-op bodies.
bool IsCogPawnClassName(const std::string& name) { return name.rfind("GearPawn_COG", 0) == 0; }

bool IsInstance(const Reader& reader, uint32_t object) {
  return reader.ObjectName(object).rfind("Default__", 0) != 0;
}

std::string DefaultPawnClassOf(const uint8_t* base, const Reader& reader, uint32_t pc) {
  const uint32_t cls = guest::LoadU32(base, pc + kGearPcDefaultPawnClass);
  return cls == 0 ? std::string("None") : reader.ObjectName(cls);
}

void NoteObject(const uint8_t* base, const Reader& reader, uint32_t object, Census* census) {
  const std::string cls = reader.ClassName(object);
  if (cls.rfind("GearPC", 0) == 0 && IsInstance(reader, object)) {
    census->pcs[reader.ObjectName(object)] = DefaultPawnClassOf(base, reader, object);
    return;
  }
  if (cls == "Class") {
    if (IsCogPawnClassName(reader.ObjectName(object))) census->pawn_classes.push_back(reader.ObjectPath(object));
    return;
  }
  if (IsPawnClassName(cls) && IsInstance(reader, object)) ++census->pawns[cls];
}

Census TakeCensus(const uint8_t* base) {
  Census census;
  const Reader reader(base);
  const uint32_t objects = guest::LoadU32(base, kGObjObjects);
  const uint32_t count = guest::LoadU32(base, kGObjObjects + 4);
  if (objects == 0 || count > kMaxObjects) return census;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t object = guest::LoadU32(base, objects + 4 * i);
    if (object != 0) NoteObject(base, reader, object, &census);
  }
  return census;
}

void FillCensus(const uint8_t* base, Census* out) { *out = TakeCensus(base); }

// The table can change under the reader (the game runs on); a fault only costs this census.
bool GuardedCensus(const uint8_t* base, Census* out) {
#ifdef _WIN32
  __try {
    FillCensus(base, out);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
#else
  FillCensus(base, out);
  return true;
#endif
}

std::string JoinPawns(const std::map<std::string, int>& pawns) {
  std::string out;
  for (const auto& [cls, n] : pawns) out += (out.empty() ? "" : ", ") + cls + " x" + std::to_string(n);
  return out.empty() ? "none" : out;
}

// One line per class: the log cuts long lines.
void LogCensus(const Census& census, const Census& last) {
  if (census.pawn_classes != last.pawn_classes) {
    REXLOG_INFO("COOPCENSUS {} COG pawn classes loaded", census.pawn_classes.size());
    for (const std::string& path : census.pawn_classes) REXLOG_INFO("COOPCENSUS class {}", path);
  }
  if (census.pawns != last.pawns) REXLOG_INFO("COOPCENSUS pawns: {}", JoinPawns(census.pawns));
  if (census.pcs != last.pcs) {
    for (const auto& [pc, pawn_class] : census.pcs)
      REXLOG_INFO("COOPCENSUS {} DefaultPawnClass={}", pc, pawn_class);
  }
}

void CensusLoop(const uint8_t* base) {
  Census last;
  last.pawn_classes.push_back("(none yet)");
  for (;;) {
    Census now;
    if (GuardedCensus(base, &now) && !(now == last)) {
      LogCensus(now, last);
      last = now;
    }
    std::this_thread::sleep_for(kPeriod);
  }
}

}  // namespace

void StartCoopCensus(const uint8_t* base) {
  static std::atomic<bool> started{false};
  if (!REXCVAR_GET(gow2_coop_census) || started.exchange(true)) return;
  std::thread(CensusLoop, base).detach();
  REXLOG_INFO("COOPCENSUS on: pawn classes and pawns are logged when they change");
}

}  // namespace rex::glue
