// gow2 - ReXGlue Recompiled Project
//
// The coop-scenes patch group of docs/COOP_STAGE2_SCENES.md (cinematics, teleports, checkpoints
// and co-op splits for players 3+). It registers itself; the hook applies it after Stage 1.

#include <optional>

#ifdef _WIN32
#include <windows.h>
#endif

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/runtime.h>

#include "gow2_guest.h"
#include "gow2_script_patch_groups.h"

REXCVAR_DEFINE_BOOL(gow2_coop_scenes, true, "Coop",
                    "Stage 2 scene patches for players 3+ (cinematics, teleports, checkpoints, "
                    "co-op splits); only with gow2_coop_players 3 or more");
REXCVAR_DEFINE_BOOL(gow2_coop_scene_probe, false, "Coop",
                    "Debug: every 2 s log each player controller's cinematic flags, pawn, "
                    "position and distance to Marcus (read-only)");

namespace rex::glue::script_patch {

namespace {

#include "gow2_script_patch_scenes_rows.inc"

constexpr uint32_t kGNames = 0x83136834;
constexpr uint32_t kGObjObjects = 0x83136860;
constexpr uint32_t kObjOuter = 40;
constexpr uint32_t kObjNameIndex = 44;
constexpr uint32_t kObjNameNumber = 48;
constexpr uint32_t kObjClass = 52;
constexpr uint32_t kNameEntryText = 8;
constexpr uint32_t kNameWideBit = 1;
constexpr uint32_t kMaxObjects = 1u << 22;
constexpr uint32_t kMaxNameLength = 128;
constexpr uint32_t kGearPcDefaultPawnClass = 1784;
constexpr uint32_t kPcCinematicFlags = 864;
constexpr uint32_t kCinematicModeMask = 0x00002000;
constexpr uint32_t kGearPcFlags = 1456;
constexpr uint32_t kInvisibleMask = 0x01000000;
constexpr uint32_t kControllerPawn = 444;
constexpr uint32_t kPawnController = 456;
constexpr uint32_t kActorLocation = 84;
constexpr uint32_t kActorHiddenFlags = 172;
constexpr uint32_t kHiddenMask = 0x40000000;
constexpr auto kProbePeriod = std::chrono::seconds(2);
constexpr int kLayoutFailSweeps = 3;

struct ProbePc {
  std::string name;
  std::string pawn_class_name_of_dpc;
  bool cinematic = false;
  bool invisible = false;
  uint32_t pawn = 0;
  std::string pawn_class;
  bool hidden = false;
  float x = 0;
  float y = 0;
  float z = 0;
};

struct ProbeSweep {
  std::vector<ProbePc> pcs;
  bool has_pawn = false;
  bool passed = false;
};

// A private copy of the census reader, so this group's files stay independent of the shared census.
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

  std::string ClassName(uint32_t object) const {
    const uint32_t cls = guest::LoadU32(base_, object + kObjClass);
    return cls == 0 ? std::string("?") : ObjectName(cls);
  }

 private:
  const uint8_t* base_;
};

bool IsProbePc(const Reader& reader, uint32_t object) {
  if (reader.ClassName(object).rfind("GearPC", 0) != 0) return false;
  return reader.ObjectName(object).rfind("Default__", 0) != 0;
}

std::string DpcName(const uint8_t* base, const Reader& reader, uint32_t pc) {
  const uint32_t cls = guest::LoadU32(base, pc + kGearPcDefaultPawnClass);
  return cls == 0 ? std::string("None") : reader.ObjectName(cls);
}

float LoadFloat(const uint8_t* base, uint32_t addr) {
  const uint32_t bits = guest::LoadU32(base, addr);
  float out = 0;
  std::memcpy(&out, &bits, sizeof(out));
  return out;
}

ProbePc ReadProbePc(const uint8_t* base, const Reader& reader, uint32_t pc) {
  ProbePc out;
  out.name = reader.ObjectName(pc);
  out.pawn_class_name_of_dpc = DpcName(base, reader, pc);
  out.cinematic = (guest::LoadU32(base, pc + kPcCinematicFlags) & kCinematicModeMask) != 0;
  out.invisible = (guest::LoadU32(base, pc + kGearPcFlags) & kInvisibleMask) != 0;
  out.pawn = guest::LoadU32(base, pc + kControllerPawn);
  if (out.pawn == 0) {
    out.pawn_class = "None";
    return out;
  }
  out.pawn_class = reader.ClassName(out.pawn);
  out.hidden = (guest::LoadU32(base, out.pawn + kActorHiddenFlags) & kHiddenMask) != 0;
  out.x = LoadFloat(base, out.pawn + kActorLocation);
  out.y = LoadFloat(base, out.pawn + kActorLocation + 4);
  out.z = LoadFloat(base, out.pawn + kActorLocation + 8);
  return out;
}

ProbeSweep TakeProbeSweep(const uint8_t* base) {
  ProbeSweep sweep;
  const Reader reader(base);
  const uint32_t objects = guest::LoadU32(base, kGObjObjects);
  const uint32_t count = guest::LoadU32(base, kGObjObjects + 4);
  if (objects == 0 || count > kMaxObjects) return sweep;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t object = guest::LoadU32(base, objects + 4 * i);
    if (object == 0 || !IsProbePc(reader, object)) continue;
    ProbePc pc = ReadProbePc(base, reader, object);
    if (pc.pawn != 0) {
      sweep.has_pawn = true;
      if (guest::LoadU32(base, pc.pawn + kPawnController) == object) sweep.passed = true;
    }
    sweep.pcs.push_back(pc);
  }
  return sweep;
}

void FillProbeSweep(const uint8_t* base, ProbeSweep* out) { *out = TakeProbeSweep(base); }

// The table can change under the reader (the game runs on); a fault only costs this sweep.
bool GuardedProbeSweep(const uint8_t* base, ProbeSweep* out) {
#ifdef _WIN32
  __try {
    FillProbeSweep(base, out);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
#else
  FillProbeSweep(base, out);
  return true;
#endif
}

bool MarcusLocation(const std::vector<ProbePc>& pcs, float* x, float* y, float* z) {
  for (const ProbePc& pc : pcs) {
    if (pc.pawn_class_name_of_dpc != "GearPawn_COGMarcus") continue;
    if (pc.pawn == 0) return false;
    *x = pc.x;
    *y = pc.y;
    *z = pc.z;
    return true;
  }
  return false;
}

std::string DistanceText(const ProbePc& pc, bool has_marcus, float mx, float my, float mz) {
  if (pc.pawn == 0 || !has_marcus) return "-";
  const double dx = double(pc.x - mx);
  const double dy = double(pc.y - my);
  const double dz = double(pc.z - mz);
  return std::to_string(std::lround(std::sqrt(dx * dx + dy * dy + dz * dz)));
}

std::string FormatProbeLine(const ProbePc& pc, const std::string& dist) {
  std::string loc = "-";
  if (pc.pawn != 0) {
    loc = std::to_string(std::lround(pc.x)) + "," + std::to_string(std::lround(pc.y)) + "," +
          std::to_string(std::lround(pc.z));
  }
  std::string line = "SCENEPROBE " + pc.name;
  line += " dpc=" + pc.pawn_class_name_of_dpc;
  line += pc.cinematic ? " cine=1" : " cine=0";
  line += pc.invisible ? " invis=1" : " invis=0";
  line += " pawn=" + pc.pawn_class;
  line += pc.hidden ? " hidden=1" : " hidden=0";
  line += " loc=" + loc;
  line += " dMarcus=" + dist;
  return line;
}

void LogChangedSweep(const std::vector<ProbePc>& pcs, std::map<std::string, std::string>* last) {
  float mx = 0;
  float my = 0;
  float mz = 0;
  const bool has_marcus = MarcusLocation(pcs, &mx, &my, &mz);
  for (const ProbePc& pc : pcs) {
    const std::string line = FormatProbeLine(pc, DistanceText(pc, has_marcus, mx, my, mz));
    auto it = last->find(pc.name);
    if (it != last->end() && it->second == line) continue;
    (*last)[pc.name] = line;
    REXLOG_INFO("{}", line);
  }
}

void SceneProbeLoop(const uint8_t* base) {
  std::map<std::string, std::string> last;
  int fails = 0;
  for (;;) {
    ProbeSweep sweep;
    if (GuardedProbeSweep(base, &sweep)) {
      if (sweep.has_pawn && !sweep.passed && ++fails >= kLayoutFailSweeps) {
        REXLOG_WARN("SCENEPROBE layout check failed (Controller.Pawn +444 / Pawn.Controller "
                    "+456); probe stopped");
        return;
      }
      if (sweep.passed) fails = 0;
      LogChangedSweep(sweep.pcs, &last);
    }
    std::this_thread::sleep_for(kProbePeriod);
  }
}

void StartSceneProbe() {
  if (!REXCVAR_GET(gow2_coop_scene_probe)) return;
  static std::atomic<bool> started{false};
  if (started.exchange(true)) return;
  const uint8_t* base =
      rex::Runtime::instance() ? rex::Runtime::instance()->virtual_membase() : nullptr;
  if (base == nullptr) return;
  std::thread(SceneProbeLoop, base).detach();
  REXLOG_INFO("SCENEPROBE on: player controllers are logged every 2 s when they change");
}

std::optional<PatchGroup> MakeCoopScenesGroup() {
  StartSceneProbe();
  if (!REXCVAR_GET(gow2_coop_scenes)) {
    REXLOG_INFO("SCRIPTPATCH coop-scenes: off (gow2_coop_scenes=false)");
    return std::nullopt;
  }
  return CoopScenesGroup();
}

[[maybe_unused]] const bool kCoopScenesRegistered =
    RegisterCoopGroup("coop-scenes", &MakeCoopScenesGroup);

}  // namespace

}  // namespace rex::glue::script_patch
