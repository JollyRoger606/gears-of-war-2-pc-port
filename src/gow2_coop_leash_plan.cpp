// gow2 - ReXGlue Recompiled Project
//
// Pure co-op leash planner, host-side and game-agnostic.
#include "gow2_coop_leash_plan.h"

#include <cmath>

namespace rex::glue::coop_leash {
namespace {

constexpr float kFallbackRings[] = {120.0f, 360.0f, 600.0f};
constexpr int kFallbackDirs[8][2] = {
    {0, -1}, {0, 1}, {-1, 0}, {1, 0}, {-1, -1}, {1, 1}, {-1, 1}, {1, -1}};
constexpr int kFallbackOffsetCount = 24;

bool PcListed(const std::vector<LeashPc>& pcs, uint32_t pc) {
  for (const LeashPc& entry : pcs) {
    if (entry.pc == pc) return true;
  }
  return false;
}

void PruneMissingPcs(const std::vector<LeashPc>& pcs, std::map<uint32_t, int>* far_seconds) {
  for (auto it = far_seconds->begin(); it != far_seconds->end();) {
    if (PcListed(pcs, it->first)) {
      ++it;
    } else {
      it = far_seconds->erase(it);
    }
  }
}

bool IsAnchorCandidate(const LeashPc& candidate, const LeashPc& extra, const LeashPc& hero,
                       float max_distance) {
  if (candidate.pc == extra.pc) return false;
  if (candidate.pawn == 0) return false;
  if (candidate.cinematic || candidate.hidden) return false;
  if (candidate.health <= 0) return false;
  if (!HasGearPawnBody(candidate)) return false;
  return Distance(candidate, hero.x, hero.y, hero.z) <= max_distance;
}

void CountFarSecond(const LeashPc& extra, const LeashPc& hero, const LeashPc& anchor, float distance,
                    const LeashConfig& config, std::map<uint32_t, int>* far_seconds,
                    std::vector<PullOrder>* orders) {
  const int counted = (*far_seconds)[extra.pc] + 1;
  if (counted < config.seconds) {
    (*far_seconds)[extra.pc] = counted;
    return;
  }
  (*far_seconds)[extra.pc] = 0;
  orders->push_back(PullOrder{extra.pc, extra.pawn, hero.pc, hero.x, hero.y, hero.z, anchor.pc,
                              anchor.x, anchor.y, anchor.z, distance});
}

void PlanExtra(const std::vector<LeashPc>& pcs, const LeashPc& extra, const LeashConfig& config,
               std::map<uint32_t, int>* far_seconds, std::vector<PullOrder>* orders) {
  const LeashPc* hero = NearestHero(pcs, extra);
  if (!IsLeashableExtra(extra) || hero == nullptr) {
    (*far_seconds)[extra.pc] = 0;
    return;
  }
  const float distance = Distance(extra, hero->x, hero->y, hero->z);
  if (distance <= config.distance) {
    (*far_seconds)[extra.pc] = 0;
    return;
  }
  const LeashPc* anchor = PickAnchor(pcs, extra, *hero, config.distance);
  CountFarSecond(extra, *hero, *anchor, distance, config, far_seconds, orders);
}

}  // namespace

bool IsHeroClass(const std::string& dpc) {
  return dpc == "GearPawn_COGMarcus" || dpc == "GearPawn_COGDom";
}

bool IsExtra(const LeashPc& p) {
  return !p.dpc.empty() && p.dpc != "None" && !IsHeroClass(p.dpc);
}

bool IsUsableHero(const LeashPc& p) {
  return IsHeroClass(p.dpc) && p.pawn != 0 && !p.cinematic && !p.hidden && p.health > 0;
}

bool IsLeashableExtra(const LeashPc& p) {
  return IsExtra(p) && p.pawn != 0 && !p.cinematic && !p.hidden && p.health > 0 &&
         HasGearPawnBody(p);
}

bool HasGearPawnBody(const LeashPc& p) { return p.pawn_class.rfind("GearPawn_", 0) == 0; }

float Distance(const LeashPc& a, float x, float y, float z) {
  const double dx = double(a.x - x);
  const double dy = double(a.y - y);
  const double dz = double(a.z - z);
  return float(std::sqrt(dx * dx + dy * dy + dz * dz));
}

const LeashPc* NearestHero(const std::vector<LeashPc>& pcs, const LeashPc& extra) {
  const LeashPc* best = nullptr;
  float best_distance = 0;
  for (const LeashPc& candidate : pcs) {
    if (!IsUsableHero(candidate)) continue;
    const float distance = Distance(extra, candidate.x, candidate.y, candidate.z);
    if (best == nullptr || distance < best_distance) {
      best = &candidate;
      best_distance = distance;
    }
  }
  return best;
}

const LeashPc* PickAnchor(const std::vector<LeashPc>& pcs, const LeashPc& extra,
                          const LeashPc& hero, float max_distance) {
  if (HasGearPawnBody(hero)) return &hero;
  const LeashPc* best = nullptr;
  float best_distance = 0;
  for (const LeashPc& candidate : pcs) {
    if (!IsAnchorCandidate(candidate, extra, hero, max_distance)) continue;
    const float distance = Distance(candidate, hero.x, hero.y, hero.z);
    if (best == nullptr || distance < best_distance) {
      best = &candidate;
      best_distance = distance;
    }
  }
  if (best == nullptr) return &hero;
  return best;
}

std::vector<PullOrder> PlanPulls(const std::vector<LeashPc>& pcs, const LeashConfig& config,
                                 std::map<uint32_t, int>* far_seconds) {
  std::vector<PullOrder> orders;
  if (far_seconds == nullptr) return orders;
  for (const LeashPc& pc : pcs) {
    if (!IsExtra(pc)) continue;
    PlanExtra(pcs, pc, config, far_seconds, &orders);
  }
  PruneMissingPcs(pcs, far_seconds);
  return orders;
}

std::vector<std::pair<float, float>> FallbackOffsets() {
  std::vector<std::pair<float, float>> offsets;
  offsets.reserve(kFallbackOffsetCount);
  for (float ring : kFallbackRings) {
    for (const auto& dir : kFallbackDirs) {
      offsets.emplace_back(float(dir[0]) * ring, float(dir[1]) * ring);
    }
  }
  return offsets;
}

}  // namespace rex::glue::coop_leash
