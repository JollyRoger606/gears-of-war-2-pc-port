// Tests for the co-op leash planner (src/gow2_coop_leash_plan.cpp): pure host-side logic.
#include "gow2_coop_leash_plan.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

using rex::glue::coop_leash::FallbackOffsets;
using rex::glue::coop_leash::LeashConfig;
using rex::glue::coop_leash::LeashPc;
using rex::glue::coop_leash::PickAnchor;
using rex::glue::coop_leash::PlanPulls;
using rex::glue::coop_leash::PullOrder;

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const char* what) {
  ++g_checks;
  if (ok) return;
  ++g_failures;
  std::printf("FAIL: %s\n", what);
}

constexpr uint32_t kMarcusPc = 11;
constexpr uint32_t kMarcusPawn = 101;
constexpr uint32_t kDomPc = 22;
constexpr uint32_t kDomPawn = 102;
constexpr uint32_t kExtraPc = 33;
constexpr uint32_t kExtraPawn = 103;
constexpr uint32_t kFootPc = 44;
constexpr uint32_t kFootPawn = 104;

LeashPc MakeHero(uint32_t pc, uint32_t pawn, const char* dpc, float x) {
  LeashPc out;
  out.pc = pc;
  out.dpc = dpc;
  out.pawn = pawn;
  out.pawn_class = dpc;
  out.health = 100;
  out.x = x;
  return out;
}

LeashPc MakeMarcus(float x) { return MakeHero(kMarcusPc, kMarcusPawn, "GearPawn_COGMarcus", x); }

LeashPc MakeDom(float x) { return MakeHero(kDomPc, kDomPawn, "GearPawn_COGDom", x); }

LeashPc MakeMarcusInCentaur(float x) {
  LeashPc out = MakeMarcus(x);
  out.pawn_class = "Vehicle_Centaur";
  return out;
}

LeashPc MakeExtraAt(uint32_t pc, uint32_t pawn, float x) {
  LeashPc out;
  out.pc = pc;
  out.dpc = "GearPawn_COGGear";
  out.pawn = pawn;
  out.pawn_class = "GearPawn_COGGear";
  out.health = 100;
  out.x = x;
  return out;
}

LeashPc MakeExtra(float x) { return MakeExtraAt(kExtraPc, kExtraPawn, x); }

LeashPc MakeFootExtra(float x) { return MakeExtraAt(kFootPc, kFootPawn, x); }

bool AnyOrdersIn(const std::vector<LeashPc>& pcs, int calls, std::map<uint32_t, int>* far) {
  const LeashConfig config;
  for (int i = 0; i < calls; ++i) {
    if (!PlanPulls(pcs, config, far).empty()) return true;
  }
  return false;
}

void TestPullAfterEightSeconds() {
  const std::vector<LeashPc> pcs{MakeMarcus(0), MakeExtra(5000)};
  std::map<uint32_t, int> far;
  Check(!AnyOrdersIn(pcs, 7, &far), "7 far seconds give no order");
  const LeashConfig config;
  const std::vector<PullOrder> orders = PlanPulls(pcs, config, &far);
  Check(orders.size() == 1, "the 8th far second gives exactly one order");
  if (orders.size() != 1) return;
  Check(orders[0].extra_pc == kExtraPc, "the order names the extra pc");
  Check(orders[0].extra_pawn == kExtraPawn, "the order names the extra pawn");
  Check(orders[0].hero_pc == kMarcusPc, "the order names Marcus");
  Check(orders[0].hero_x == 0 && orders[0].hero_y == 0 && orders[0].hero_z == 0,
        "the order carries the hero position");
  Check(orders[0].anchor_pc == kMarcusPc, "the anchor is Marcus on foot");
  Check(orders[0].anchor_x == 0 && orders[0].anchor_y == 0 && orders[0].anchor_z == 0,
        "the order carries the anchor position");
  Check(std::fabs(orders[0].distance - 5000) < 1, "the order carries the distance");
  Check(far[kExtraPc] == 0, "the counter is back to 0 after the pull");
}

void TestReturnResetsCounter() {
  const LeashConfig config;
  std::map<uint32_t, int> far;
  const std::vector<LeashPc> away{MakeMarcus(0), MakeExtra(5000)};
  Check(!AnyOrdersIn(away, 5, &far), "5 far seconds give no order");
  Check(far[kExtraPc] == 5, "the counter reaches 5");
  const std::vector<LeashPc> back{MakeMarcus(0), MakeExtra(1000)};
  Check(PlanPulls(back, config, &far).empty(), "back within range gives no order");
  Check(far[kExtraPc] == 0, "coming back resets the counter to 0");
  Check(!AnyOrdersIn(away, 7, &far), "7 more far seconds give no order");
  Check(PlanPulls(away, config, &far).size() == 1, "8 more far seconds pull again");
}

void CheckVariantNeverPulled(LeashPc extra, const char* no_order, const char* counter_zero) {
  const LeashConfig config;
  std::map<uint32_t, int> far;
  const std::vector<LeashPc> pcs{MakeMarcus(0), extra};
  bool ordered = false;
  for (int i = 0; i < 10; ++i) {
    if (!PlanPulls(pcs, config, &far).empty()) ordered = true;
  }
  Check(!ordered, no_order);
  Check(far[kExtraPc] == 0, counter_zero);
}

void TestUnleashableExtras() {
  LeashPc cinematic = MakeExtra(5000);
  cinematic.cinematic = true;
  CheckVariantNeverPulled(cinematic, "a cinematic extra gets no order",
                           "a cinematic extra keeps counter 0");
  LeashPc hidden = MakeExtra(5000);
  hidden.hidden = true;
  CheckVariantNeverPulled(hidden, "a hidden extra gets no order",
                           "a hidden extra keeps counter 0");
  LeashPc dead = MakeExtra(5000);
  dead.health = 0;
  CheckVariantNeverPulled(dead, "a dead extra gets no order", "a dead extra keeps counter 0");
  LeashPc pawnless = MakeExtra(5000);
  pawnless.pawn = 0;
  CheckVariantNeverPulled(pawnless, "an extra without pawn gets no order",
                           "an extra without pawn keeps counter 0");
  LeashPc driver = MakeExtra(5000);
  driver.pawn_class = "Vehicle_Centaur";
  CheckVariantNeverPulled(driver, "an extra in a vehicle gets no order",
                           "an extra in a vehicle keeps counter 0");
}

void TestNearestHero() {
  const LeashConfig config;
  const std::vector<LeashPc> pcs{MakeMarcus(9000), MakeDom(4500), MakeExtra(0)};
  std::map<uint32_t, int> far;
  Check(!AnyOrdersIn(pcs, 7, &far), "7 far seconds give no order with two heroes");
  const std::vector<PullOrder> orders = PlanPulls(pcs, config, &far);
  Check(orders.size() == 1, "the 8th far second pulls towards the nearer hero");
  if (orders.size() == 1) {
    Check(orders[0].hero_pc == kDomPc, "the nearer hero Dom is named");
    Check(orders[0].hero_x == 4500, "the order carries Dom's position");
    Check(orders[0].anchor_pc == kDomPc, "the anchor is Dom on foot");
  }
  std::vector<LeashPc> dom_cine = pcs;
  dom_cine[1].cinematic = true;
  std::map<uint32_t, int> far2;
  Check(!AnyOrdersIn(dom_cine, 7, &far2), "7 far seconds give no order with Dom cinematic");
  const std::vector<PullOrder> orders2 = PlanPulls(dom_cine, config, &far2);
  Check(orders2.size() == 1, "the 8th far second still pulls with Dom cinematic");
  if (orders2.size() == 1) {
    Check(orders2[0].hero_pc == kMarcusPc, "Marcus is used when Dom is cinematic");
    Check(orders2[0].anchor_pc == kMarcusPc, "the anchor is Marcus on foot");
  }
}

void TestNoUsableHero() {
  LeashPc marcus = MakeMarcus(0);
  marcus.hidden = true;
  const std::vector<LeashPc> pcs{marcus, MakeExtra(5000)};
  std::map<uint32_t, int> far;
  const LeashConfig config;
  bool ordered = false;
  for (int i = 0; i < 10; ++i) {
    if (!PlanPulls(pcs, config, &far).empty()) ordered = true;
  }
  Check(!ordered, "no usable hero gives no order");
  Check(far[kExtraPc] == 0, "no usable hero keeps counter 0");
}

void TestHeroesAndNoneNeverPulled() {
  LeashPc none = MakeExtra(9000);
  none.pc = 44;
  none.dpc = "None";
  LeashPc empty = MakeExtra(9000);
  empty.pc = 55;
  empty.dpc.clear();
  const std::vector<LeashPc> pcs{MakeMarcus(0), MakeDom(9000), none, empty};
  std::map<uint32_t, int> far;
  const LeashConfig config;
  bool ordered = false;
  for (int i = 0; i < 10; ++i) {
    if (!PlanPulls(pcs, config, &far).empty()) ordered = true;
  }
  Check(!ordered, "heroes and None or empty dpc never get orders");
  Check(far.empty(), "heroes and None or empty dpc leave no counters");
}

void TestDisappearedPcPruned() {
  const LeashConfig config;
  std::map<uint32_t, int> far;
  const std::vector<LeashPc> both{MakeMarcus(0), MakeExtra(5000)};
  PlanPulls(both, config, &far);
  Check(far.find(kExtraPc) != far.end(), "a far extra is tracked");
  const std::vector<LeashPc> alone{MakeMarcus(0)};
  Check(PlanPulls(alone, config, &far).empty(), "Marcus alone gives no order");
  Check(far.find(kExtraPc) == far.end(), "a pc that disappears is erased");
}

void TestAnchorIsHeroWhenHeroOnFoot() {
  const LeashPc hero = MakeMarcus(0);
  const LeashPc extra = MakeExtra(5000);
  const std::vector<LeashPc> pcs{hero, extra};
  const LeashPc* anchor = PickAnchor(pcs, extra, pcs[0], LeashConfig().distance);
  Check(anchor != nullptr, "PickAnchor returns someone when the hero is on foot");
  if (anchor == nullptr) return;
  Check(anchor->pc == kMarcusPc, "the anchor is the hero on foot");
}

void TestAnchorIsFootExtraWhenHeroDrives() {
  const std::vector<LeashPc> pcs{MakeMarcusInCentaur(0), MakeFootExtra(312), MakeExtra(17517)};
  std::map<uint32_t, int> far;
  Check(!AnyOrdersIn(pcs, 7, &far), "7 far seconds give no order when the hero drives");
  const LeashConfig config;
  const std::vector<PullOrder> orders = PlanPulls(pcs, config, &far);
  Check(orders.size() == 1, "the 8th far second pulls the stranded extra");
  if (orders.size() != 1) return;
  Check(orders[0].hero_pc == kMarcusPc, "the hero is still Marcus in the tank");
  Check(orders[0].anchor_pc == kFootPc, "the anchor is the extra on foot");
  Check(orders[0].anchor_x == 312 && orders[0].anchor_y == 0 && orders[0].anchor_z == 0,
        "the anchor carries the on-foot position");
  Check(std::fabs(orders[0].distance - 17517) < 1, "the distance is measured from Marcus");
}

void TestAnchorFallsBackToHero() {
  const LeashConfig config;
  LeashPc foot_cine = MakeFootExtra(312);
  foot_cine.cinematic = true;
  const std::vector<LeashPc> cine{MakeMarcusInCentaur(0), foot_cine, MakeExtra(17517)};
  std::map<uint32_t, int> far_cine;
  Check(!AnyOrdersIn(cine, 7, &far_cine), "7 far seconds give no order with a cinematic bystander");
  const std::vector<PullOrder> cine_orders = PlanPulls(cine, config, &far_cine);
  Check(cine_orders.size() == 1, "the 8th far second pulls with a cinematic bystander");
  if (cine_orders.size() == 1) {
    Check(cine_orders[0].anchor_pc == kMarcusPc, "a cinematic bystander is not the anchor");
  }
  const std::vector<LeashPc> away{MakeMarcusInCentaur(0), MakeFootExtra(5000), MakeExtra(17517)};
  std::map<uint32_t, int> far_away;
  Check(!AnyOrdersIn(away, 7, &far_away), "7 far seconds give no order with a far bystander");
  const std::vector<PullOrder> away_orders = PlanPulls(away, config, &far_away);
  Check(away_orders.size() == 2, "both far extras get orders with a far bystander");
  for (const PullOrder& order : away_orders) {
    Check(order.anchor_pc == kMarcusPc, "a bystander 5000 away is not the anchor");
  }
}

void TestStrandedExtraNeverOwnAnchor() {
  const std::vector<LeashPc> pcs{MakeMarcusInCentaur(0), MakeExtra(17517)};
  std::map<uint32_t, int> far;
  Check(!AnyOrdersIn(pcs, 7, &far), "7 far seconds give no order with no bystander");
  const LeashConfig config;
  const std::vector<PullOrder> orders = PlanPulls(pcs, config, &far);
  Check(orders.size() == 1, "the 8th far second pulls with no bystander");
  if (orders.size() != 1) return;
  Check(orders[0].anchor_pc != kExtraPc, "the stranded extra is never its own anchor");
  Check(orders[0].anchor_pc == kMarcusPc, "the anchor falls back to the hero");
}

void TestFallbackOffsets() {
  const std::vector<std::pair<float, float>> offsets = FallbackOffsets();
  Check(offsets.size() == 24, "FallbackOffsets has 24 entries");
  if (offsets.size() != 24) return;
  Check(offsets[0].first == 0 && offsets[0].second == -120, "the first offset is (0,-120)");
  Check(offsets[8].first == 0 && offsets[8].second == -360, "entry 8 is (0,-360)");
  Check(offsets[16].first == 0 && offsets[16].second == -600, "entry 16 is (0,-600)");
}

}  // namespace

int main() {
  TestPullAfterEightSeconds();
  TestReturnResetsCounter();
  TestUnleashableExtras();
  TestNearestHero();
  TestNoUsableHero();
  TestHeroesAndNoneNeverPulled();
  TestDisappearedPcPruned();
  TestAnchorIsHeroWhenHeroOnFoot();
  TestAnchorIsFootExtraWhenHeroDrives();
  TestAnchorFallsBackToHero();
  TestStrandedExtraNeverOwnAnchor();
  TestFallbackOffsets();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
