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

void PruneState(const std::vector<LeashPc>& pcs, LeashState* state) {
  for (auto it = state->far_seconds.begin(); it != state->far_seconds.end();) {
    if (PcListed(pcs, it->first)) {
      ++it;
    } else {
      it = state->far_seconds.erase(it);
    }
  }
  for (auto it = state->immediate_cooldown.begin(); it != state->immediate_cooldown.end();) {
    if (PcListed(pcs, it->first)) {
      ++it;
    } else {
      it = state->immediate_cooldown.erase(it);
    }
  }
  for (auto it = state->god_granted.begin(); it != state->god_granted.end();) {
    if (PcListed(pcs, *it)) {
      ++it;
    } else {
      it = state->god_granted.erase(it);
    }
  }
}

void PruneGranted(const std::vector<LeashPc>& pcs, std::set<uint32_t>* granted) {
  for (auto it = granted->begin(); it != granted->end();) {
    if (PcListed(pcs, *it)) {
      ++it;
    } else {
      it = granted->erase(it);
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

bool IsOverlapBody(const LeashPc& candidate, uint32_t self) {
  if (candidate.pc == self) return false;
  if (candidate.pawn == 0) return false;
  if (candidate.health <= 0) return false;
  return HasGearPawnBody(candidate);
}

const LeashPc* FindOverlap(const std::vector<LeashPc>& pcs, const LeashPc& extra) {
  for (const LeashPc& candidate : pcs) {
    if (!IsOverlapBody(candidate, extra.pc)) continue;
    if (Distance(extra, candidate.x, candidate.y, candidate.z) <= kOverlapDistance) {
      return &candidate;
    }
  }
  return nullptr;
}

bool IsFellBelowHeroes(const std::vector<LeashPc>& pcs, const LeashPc& extra) {
  bool any_hero = false;
  for (const LeashPc& candidate : pcs) {
    if (!IsUsableHero(candidate)) continue;
    any_hero = true;
    if (!(extra.z < candidate.z - kFellBelowHeroes)) return false;
  }
  return any_hero;
}

PullOrder MakeOverlapOrder(const LeashPc& extra, const LeashPc& hero, const LeashPc& anchor,
                           float distance, const LeashPc& overlapped) {
  PullOrder order{extra.pc, extra.pawn, hero.pc, hero.x, hero.y, hero.z, anchor.pc,
                  anchor.x, anchor.y, anchor.z, distance};
  order.reason = PullReason::kOverlap;
  order.overlap_pc = overlapped.pc;
  order.overlap_x = overlapped.x;
  order.overlap_y = overlapped.y;
  order.overlap_z = overlapped.z;
  return order;
}

PullOrder MakeFellOrder(const LeashPc& extra, const LeashPc& hero, const LeashPc& anchor,
                        float distance) {
  PullOrder order{extra.pc, extra.pawn, hero.pc, hero.x, hero.y, hero.z, anchor.pc,
                  anchor.x, anchor.y, anchor.z, distance};
  order.reason = PullReason::kFell;
  return order;
}

bool CooldownActive(const LeashState* state, uint32_t pc) {
  auto it = state->immediate_cooldown.find(pc);
  return it != state->immediate_cooldown.end() && it->second > 0;
}

void TickCooldown(LeashState* state, uint32_t pc) {
  auto it = state->immediate_cooldown.find(pc);
  if (it == state->immediate_cooldown.end()) return;
  if (--it->second <= 0) state->immediate_cooldown.erase(it);
}

bool TryImmediate(const std::vector<LeashPc>& pcs, const LeashPc& extra, const LeashPc& hero,
                  const LeashConfig& config, LeashState* state, std::vector<PullOrder>* orders) {
  const LeashPc* anchor = PickAnchor(pcs, extra, hero, config.distance);
  const float distance = Distance(extra, hero.x, hero.y, hero.z);
  if (const LeashPc* overlapped = FindOverlap(pcs, extra)) {
    orders->push_back(MakeOverlapOrder(extra, hero, *anchor, distance, *overlapped));
    state->immediate_cooldown[extra.pc] = config.seconds;
    return true;
  }
  if (IsFellBelowHeroes(pcs, extra)) {
    orders->push_back(MakeFellOrder(extra, hero, *anchor, distance));
    state->immediate_cooldown[extra.pc] = config.seconds;
    return true;
  }
  return false;
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

void PlanFar(const std::vector<LeashPc>& pcs, const LeashPc& extra, const LeashPc& hero,
             const LeashConfig& config, LeashState* state, std::vector<PullOrder>* orders) {
  const float distance = Distance(extra, hero.x, hero.y, hero.z);
  if (distance <= config.distance) {
    state->far_seconds[extra.pc] = 0;
    return;
  }
  const LeashPc* anchor = PickAnchor(pcs, extra, hero, config.distance);
  CountFarSecond(extra, hero, *anchor, distance, config, &state->far_seconds, orders);
}

void PlanExtra(const std::vector<LeashPc>& pcs, const LeashPc& extra, const LeashConfig& config,
               LeashState* state, std::vector<PullOrder>* orders) {
  const LeashPc* hero = NearestHero(pcs, extra);
  if (!IsLeashableExtra(extra) || hero == nullptr) {
    state->far_seconds[extra.pc] = 0;
    if (CooldownActive(state, extra.pc)) {
      TickCooldown(state, extra.pc);
    } else {
      state->immediate_cooldown.erase(extra.pc);
    }
    return;
  }
  if (CooldownActive(state, extra.pc)) {
    TickCooldown(state, extra.pc);
  } else {
    state->immediate_cooldown.erase(extra.pc);
    if (TryImmediate(pcs, extra, *hero, config, state, orders)) return;
  }
  PlanFar(pcs, extra, *hero, config, state, orders);
}

bool AnyRidingHero(const std::vector<LeashPc>& pcs) {
  for (const LeashPc& pc : pcs) {
    if (IsRidingHero(pc)) return true;
  }
  return false;
}

void CollectGrants(const std::vector<LeashPc>& pcs, std::set<uint32_t>* granted,
                   GodOrders* orders) {
  for (const LeashPc& pc : pcs) {
    if (!IsExtraOnFoot(pc)) continue;
    if (!pc.god) {
      orders->grant.push_back(pc.pc);
      granted->insert(pc.pc);
    } else {
      granted->insert(pc.pc);
    }
  }
}

void CollectRevokes(const std::vector<LeashPc>& pcs, const std::set<uint32_t>& granted,
                    GodOrders* orders) {
  for (const LeashPc& pc : pcs) {
    if (granted.count(pc.pc) != 0 && pc.god) orders->revoke.push_back(pc.pc);
  }
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

bool IsRidingHero(const LeashPc& p) { return IsHeroClass(p.dpc) && p.pawn != 0 && p.riding; }

bool IsExtraOnFoot(const LeashPc& p) {
  return IsExtra(p) && p.pawn != 0 && p.health > 0 && HasGearPawnBody(p) && !p.riding;
}

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
                                 LeashState* state) {
  std::vector<PullOrder> orders;
  if (state == nullptr) return orders;
  for (const LeashPc& pc : pcs) {
    if (!IsExtra(pc)) continue;
    PlanExtra(pcs, pc, config, state, &orders);
  }
  PruneState(pcs, state);
  return orders;
}

GodOrders PlanGodMode(const std::vector<LeashPc>& pcs, std::set<uint32_t>* granted) {
  GodOrders orders;
  if (granted == nullptr) return orders;
  PruneGranted(pcs, granted);
  if (AnyRidingHero(pcs)) {
    CollectGrants(pcs, granted, &orders);
    return orders;
  }
  CollectRevokes(pcs, *granted, &orders);
  granted->clear();
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
