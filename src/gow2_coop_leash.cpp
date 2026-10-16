// gow2 - ReXGlue Recompiled Project
//
// Host-side leash for co-op players 3+: whoever stays far from the nearest human hero is moved
// next to him (spot pull, else one of 8 fallback spots).
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "gow2_coop_leash_plan.h"
#include "gow2_dev_commands.h"
#include "gow2_dev_guest_calls.h"
#include "gow2_engine_tick.h"
#include "gow2_guest.h"

REXCVAR_DEFINE_BOOL(gow2_coop_leash, true, "Coop",
                    "Players 3+ who stay far from the nearest human Marcus or Dom are moved next "
                    "to him (host only; gow2_coop_players 3 or more)");
REXCVAR_DEFINE_DOUBLE(gow2_coop_leash_distance, 4000.0, "Coop",
                      "Distance in game units beyond which a player 3+ counts as left behind");
REXCVAR_DEFINE_INT32(gow2_coop_leash_seconds, 8, "Coop",
                     "Seconds a player 3+ must stay left behind before the leash moves him");
REXCVAR_DECLARE(int32_t, gow2_coop_players);

namespace rex::glue {
namespace {

// Guest layout, measured in the running game.
constexpr uint32_t kGearPcDefaultPawnClass = 1784;
constexpr uint32_t kPcCinematicFlags = 864;
constexpr uint32_t kCinematicModeMask = 0x00002000;
constexpr uint32_t kControllerPawn = 444;
constexpr uint32_t kPawnHealth = 692;
constexpr uint32_t kActorHiddenFlags = 172;
constexpr uint32_t kHiddenMask = 0x40000000;
constexpr uint32_t kActorLocation = 84;
constexpr uint32_t kActorRotation = 96;
constexpr int kMinLeashPlayers = 3;
constexpr auto kLeashPeriod = std::chrono::seconds(1);
constexpr uint8_t kPhysicsFalling = 2;
constexpr uint32_t kSetLocationOutBytes = 16;
constexpr uint32_t kSetLocationReturnOffset = 12;

struct LeashSettings {
  bool ready = false;
  bool enabled = false;
  float distance = 0;
  int seconds = 0;
};

LeashSettings& TheSettings() {
  static LeashSettings settings;
  return settings;
}

void InitSettings(LeashSettings* settings) {
  if (settings->ready) return;
  settings->ready = true;
  if (!REXCVAR_GET(gow2_coop_leash)) return;
  if (REXCVAR_GET(gow2_coop_players) < kMinLeashPlayers) return;
  settings->enabled = true;
  settings->distance = float(REXCVAR_GET(gow2_coop_leash_distance));
  settings->seconds = REXCVAR_GET(gow2_coop_leash_seconds);
  REXLOG_INFO("COOPLEASH on: players 3+ further than {} units from the nearest human hero for "
              "{} s are moved next to him",
              settings->distance, settings->seconds);
}

bool TakeSnapshotTurn() {
  static std::chrono::steady_clock::time_point last{};
  const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
  if (now - last < kLeashPeriod) return false;
  last = now;
  return true;
}

float LoadFloat(const uint8_t* base, uint32_t addr) {
  const uint32_t bits = guest::LoadU32(base, addr);
  float out = 0;
  std::memcpy(&out, &bits, sizeof(out));
  return out;
}

uint32_t FloatBits(float value) {
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

void AppendU32Be(uint32_t value, std::vector<uint8_t>* out) {
  out->push_back(uint8_t(value >> 24));
  out->push_back(uint8_t(value >> 16));
  out->push_back(uint8_t(value >> 8));
  out->push_back(uint8_t(value));
}

uint32_t DecodeU32Be(const std::vector<uint8_t>& bytes, size_t at) {
  return (uint32_t(bytes[at]) << 24) | (uint32_t(bytes[at + 1]) << 16) |
         (uint32_t(bytes[at + 2]) << 8) | bytes[at + 3];
}

std::vector<uint8_t> EncodeU32Be(uint32_t value) {
  std::vector<uint8_t> out;
  AppendU32Be(value, &out);
  return out;
}

std::vector<uint8_t> EncodeVectorBe(float x, float y, float z) {
  std::vector<uint8_t> out;
  AppendU32Be(FloatBits(x), &out);
  AppendU32Be(FloatBits(y), &out);
  AppendU32Be(FloatBits(z), &out);
  return out;
}

std::string DefaultPawnClassName(const uint8_t* base, uint32_t pc) {
  const uint32_t cls = guest::LoadU32(base, pc + kGearPcDefaultPawnClass);
  if (cls == 0) return "None";
  return dev_commands::ObjectName(base, dev_commands::kGameGlobals, cls);
}

void FillPawnFields(const uint8_t* base, uint32_t pawn, coop_leash::LeashPc* out) {
  out->pawn_class = dev_commands::ClassName(base, dev_commands::kGameGlobals, pawn);
  out->health = int32_t(guest::LoadU32(base, pawn + kPawnHealth));
  out->hidden = (guest::LoadU32(base, pawn + kActorHiddenFlags) & kHiddenMask) != 0;
  out->x = LoadFloat(base, pawn + kActorLocation);
  out->y = LoadFloat(base, pawn + kActorLocation + 4);
  out->z = LoadFloat(base, pawn + kActorLocation + 8);
}

coop_leash::LeashPc ReadLeashPc(const uint8_t* base, uint32_t pc) {
  coop_leash::LeashPc out;
  out.pc = pc;
  out.dpc = DefaultPawnClassName(base, pc);
  out.cinematic = (guest::LoadU32(base, pc + kPcCinematicFlags) & kCinematicModeMask) != 0;
  out.pawn = guest::LoadU32(base, pc + kControllerPawn);
  if (out.pawn != 0) FillPawnFields(base, out.pawn, &out);
  return out;
}

std::optional<std::vector<coop_leash::LeashPc>> TakeSnapshot(const uint8_t* base) {
  const uint32_t world_info = dev_commands::WorldInfoOf(base, dev_commands::kGameGlobals);
  if (world_info == 0) return std::nullopt;
  if (guest::LoadU8(base, world_info + dev_commands::layout::kWorldInfoNetMode) ==
      dev_commands::layout::kNetModeClient) {
    return std::nullopt;
  }
  std::vector<coop_leash::LeashPc> pcs;
  for (uint32_t pc : dev_commands::PlayerControllers(base, dev_commands::kGameGlobals, world_info)) {
    pcs.push_back(ReadLeashPc(base, pc));
  }
  return pcs;
}

std::optional<std::vector<coop_leash::LeashPc>> GuardedSnapshot(const uint8_t* base) {
  std::optional<std::vector<coop_leash::LeashPc>> out;
  auto read = [&] { out = TakeSnapshot(base); };
  if (!guest_call::Guarded(read)) return std::nullopt;
  return out;
}

bool ExtraIsClose(const uint8_t* base, const coop_leash::PullOrder& order) {
  bool close = false;
  auto read = [&] {
    const float x = LoadFloat(base, order.extra_pawn + kActorLocation);
    const float y = LoadFloat(base, order.extra_pawn + kActorLocation + 4);
    const float z = LoadFloat(base, order.extra_pawn + kActorLocation + 8);
    const double dx = double(x - order.anchor_x);
    const double dy = double(y - order.anchor_y);
    const double dz = double(z - order.anchor_z);
    close = std::sqrt(dx * dx + dy * dy + dz * dz) <= coop_leash::kPulledCloseEnough;
  };
  if (!guest_call::Guarded(read)) return false;
  return close;
}

std::string PcName(const uint8_t* base, uint32_t pc) {
  return dev_commands::ObjectName(base, dev_commands::kGameGlobals, pc);
}

std::string AnchorLabel(const uint8_t* base, const coop_leash::PullOrder& order) {
  const std::string anchor = PcName(base, order.anchor_pc);
  if (order.anchor_pc == order.hero_pc) return anchor;
  return anchor + " near " + PcName(base, order.hero_pc);
}

bool TrySpotPull(const PPCContext& ctx, uint8_t* base, const coop_leash::PullOrder& order) {
  static std::optional<uint64_t> name =
      guest_call::MakeName(ctx, base, "FindAvailableTeleportSpot", false);
  if (!name.has_value()) return false;
  const uint32_t fn = guest_call::FindFunction(ctx, base, order.anchor_pc, *name);
  // TestPawn @0, TeleportAnchor @4, bSkipRadiusSearch @8, return @12; zero is None/false.
  if (fn != 0) {
    guest_call::ProcessEventWithParms(ctx, base, order.anchor_pc, fn, EncodeU32Be(order.extra_pawn),
                                      nullptr, 0);
  }
  if (!ExtraIsClose(base, order)) return false;
  REXLOG_INFO("COOPLEASH {} moved next to {} (was {} units away)", PcName(base, order.extra_pc),
              AnchorLabel(base, order), std::lround(order.distance));
  return true;
}

bool PlaceExtraAt(const PPCContext& ctx, uint8_t* base, uint32_t pawn, uint32_t fn, float x,
                  float y, float z) {
  std::vector<uint8_t> out;
  if (!guest_call::ProcessEventWithParms(ctx, base, pawn, fn, EncodeVectorBe(x, y, z), &out,
                                         kSetLocationOutBytes)) {
    return false;
  }
  if (out.size() < kSetLocationOutBytes) return false;
  return DecodeU32Be(out, kSetLocationReturnOffset) != 0;
}

void SetFalling(const PPCContext& ctx, uint8_t* base, uint32_t pawn,
                const std::optional<uint64_t>& name) {
  if (!name.has_value()) return;
  const uint32_t fn = guest_call::FindFunction(ctx, base, pawn, *name);
  if (fn == 0) return;
  guest_call::ProcessEventWithParms(ctx, base, pawn, fn, {kPhysicsFalling, 0, 0, 0}, nullptr, 0);
}

void SendClientLocation(const PPCContext& ctx, uint8_t* base, const coop_leash::PullOrder& order,
                        float x, float y, float z, const std::optional<uint64_t>& name) {
  if (!name.has_value()) return;
  const uint32_t fn = guest_call::FindFunction(ctx, base, order.extra_pc, *name);
  if (fn == 0) return;
  std::vector<uint8_t> parms = EncodeVectorBe(x, y, z);
  AppendU32Be(guest::LoadU32(base, order.extra_pawn + kActorRotation), &parms);
  AppendU32Be(guest::LoadU32(base, order.extra_pawn + kActorRotation + 4), &parms);
  AppendU32Be(guest::LoadU32(base, order.extra_pawn + kActorRotation + 8), &parms);
  guest_call::ProcessEventWithParms(ctx, base, order.extra_pc, fn, parms, nullptr, 0);
}

bool TryPlaceAt(const PPCContext& ctx, uint8_t* base, const coop_leash::PullOrder& order,
                uint32_t place_fn, float dx, float dy, const std::optional<uint64_t>& physics_name,
                const std::optional<uint64_t>& client_name) {
  const float x = order.anchor_x + dx;
  const float y = order.anchor_y + dy;
  const float z = order.anchor_z + coop_leash::kFallbackRaiseZ;
  if (!PlaceExtraAt(ctx, base, order.extra_pawn, place_fn, x, y, z)) return false;
  SetFalling(ctx, base, order.extra_pawn, physics_name);
  SendClientLocation(ctx, base, order, x, y, z, client_name);
  REXLOG_INFO("COOPLEASH {} placed {},{} from {} (was {} units away; no navigation point near him)",
              PcName(base, order.extra_pc), dx, dy, AnchorLabel(base, order),
              std::lround(order.distance));
  return true;
}

bool TryFallbackPull(const PPCContext& ctx, uint8_t* base, const coop_leash::PullOrder& order) {
  static std::optional<uint64_t> set_location =
      guest_call::MakeName(ctx, base, "SetLocation", false);
  static std::optional<uint64_t> set_physics =
      guest_call::MakeName(ctx, base, "SetPhysics", false);
  static std::optional<uint64_t> client_set_location =
      guest_call::MakeName(ctx, base, "ClientSetLocation", false);
  if (!set_location.has_value()) return false;
  const uint32_t place_fn = guest_call::FindFunction(ctx, base, order.extra_pawn, *set_location);
  if (place_fn == 0) return false;
  for (const auto [dx, dy] : coop_leash::FallbackOffsets()) {
    if (TryPlaceAt(ctx, base, order, place_fn, dx, dy, set_physics, client_set_location)) {
      return true;
    }
  }
  return false;
}

void PullExtra(const PPCContext& ctx, uint8_t* base, const coop_leash::PullOrder& order) {
  if (TrySpotPull(ctx, base, order)) return;
  if (TryFallbackPull(ctx, base, order)) return;
  REXLOG_WARN("COOPLEASH {}: no free spot next to {} ({} units away)", PcName(base, order.extra_pc),
              PcName(base, order.hero_pc), std::lround(order.distance));
}

void LeashTick(const PPCContext& ctx, uint8_t* base) {
  LeashSettings& settings = TheSettings();
  InitSettings(&settings);
  if (!settings.enabled) return;
  if (!TakeSnapshotTurn()) return;
  const std::optional<std::vector<coop_leash::LeashPc>> snapshot = GuardedSnapshot(base);
  if (!snapshot.has_value()) return;
  static std::map<uint32_t, int> far_seconds;
  const coop_leash::LeashConfig config{settings.distance, settings.seconds};
  const std::vector<coop_leash::PullOrder> orders =
      coop_leash::PlanPulls(*snapshot, config, &far_seconds);
  for (const coop_leash::PullOrder& order : orders) PullExtra(ctx, base, order);
}

[[maybe_unused]] const bool kLeashRegistered =
    (rex::glue::AddEngineTickListener(&LeashTick), true);

}  // namespace
}  // namespace rex::glue
