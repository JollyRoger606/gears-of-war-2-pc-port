// gow2 - ReXGlue Recompiled Project
//
// Pure co-op leash planner: which extras stayed far from a human hero.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace rex::glue::coop_leash {

constexpr float kPulledCloseEnough = 1500.0f;
constexpr float kFallbackRaiseZ = 40.0f;

struct LeashPc {
  uint32_t pc = 0;
  std::string dpc;
  bool cinematic = false;
  uint32_t pawn = 0;
  std::string pawn_class;
  int32_t health = 0;
  bool hidden = false;
  float x = 0, y = 0, z = 0;
};

struct PullOrder {
  uint32_t extra_pc = 0;
  uint32_t extra_pawn = 0;
  uint32_t hero_pc = 0;
  float hero_x = 0, hero_y = 0, hero_z = 0;
  uint32_t anchor_pc = 0;
  float anchor_x = 0, anchor_y = 0, anchor_z = 0;
  float distance = 0;
};

struct LeashConfig {
  float distance = 4000.0f;
  int seconds = 8;
};

bool IsHeroClass(const std::string& dpc);
bool IsExtra(const LeashPc& p);
bool IsUsableHero(const LeashPc& p);
bool IsLeashableExtra(const LeashPc& p);
bool HasGearPawnBody(const LeashPc& p);
float Distance(const LeashPc& a, float x, float y, float z);
const LeashPc* NearestHero(const std::vector<LeashPc>& pcs, const LeashPc& extra);
const LeashPc* PickAnchor(const std::vector<LeashPc>& pcs, const LeashPc& extra, const LeashPc& hero,
                          float max_distance);
std::vector<PullOrder> PlanPulls(const std::vector<LeashPc>& pcs, const LeashConfig& config,
                                 std::map<uint32_t, int>* far_seconds);
std::vector<std::pair<float, float>> FallbackOffsets();

}  // namespace rex::glue::coop_leash
