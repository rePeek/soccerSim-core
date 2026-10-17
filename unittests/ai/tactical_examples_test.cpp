// TACT-8: the three pure-data example plans validate and describe the intended
// behaviours (balanced, dual-phase shape change, low block counter).
#include <catch2/catch_test_macros.hpp>

#include "football/ai/tactical_examples.hpp"

namespace {

using football::ai::MakeBalanced433;
using football::ai::MakeLowBlockCounter;
using football::ai::MakeThreeTwoFiveVsFourFourTwo;
using football::ai::TransitionOnWin;
using football::ai::ValidateTacticalPlan;

}  // namespace

TEST_CASE("the 4-3-3 balanced example is valid", "[ai][tactical][examples]") {
  const auto plan = MakeBalanced433();
  CHECK(ValidateTacticalPlan(plan).ok);
  CHECK(plan.mentality == football::ai::Mentality::Balanced);
  // Three defenders, three midfielders, three forwards.
  int defenders = 0;
  for (std::size_t i = 1; i <= 4; ++i)
    if (plan.slots[i].in_possession_role ==
        football::ai::PossessionRole::FullBack ||
        plan.slots[i].in_possession_role ==
        football::ai::PossessionRole::CentreBack)
      ++defenders;
  CHECK(defenders == 4);
}

TEST_CASE("the 3-2-5 / 4-4-2 example changes shape by phase",
          "[ai][tactical][examples]") {
  const auto plan = MakeThreeTwoFiveVsFourFourTwo();
  CHECK(ValidateTacticalPlan(plan).ok);
  // At least the wide slots move between phases.
  bool phase_differs = false;
  for (const auto& slot : plan.slots) {
    if (slot.in_possession.u != slot.out_of_possession.u ||
        slot.in_possession.v != slot.out_of_possession.v)
      phase_differs = true;
  }
  CHECK(phase_differs);
  // In possession the wing-backs are high; out of possession they drop.
  CHECK(plan.slots[6].in_possession.u > plan.slots[6].out_of_possession.u);
  CHECK(plan.slots[8].out_of_possession_role ==
        football::ai::DefendingRole::PressingMidfielder);
}

TEST_CASE("the low block counter example is deep and direct",
          "[ai][tactical][examples]") {
  const auto plan = MakeLowBlockCounter();
  CHECK(ValidateTacticalPlan(plan).ok);
  CHECK(plan.out_of_possession.defensive_line < 0.3f);
  CHECK(plan.out_of_possession.compactness > 0.7f);
  CHECK(plan.out_of_possession.pressing_intensity < 0.4f);
  CHECK(plan.transition.on_win == TransitionOnWin::Counter);
  CHECK(plan.in_possession.build_up == football::ai::BuildUpStyle::Direct);
}
