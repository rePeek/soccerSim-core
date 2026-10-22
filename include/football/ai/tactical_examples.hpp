#ifndef FOOTBALL_AI_TACTICAL_EXAMPLES_HPP
#define FOOTBALL_AI_TACTICAL_EXAMPLES_HPP

#include <cstddef>

#include "football/ai/tactical_plan.hpp"

namespace football::ai {

// Small, pure-data example plans. They are values, not files or UI presets:
// callers may copy, edit and validate them. Anchors are canonical (u from own
// goal to opponent goal, v left/right from the attacking view).

namespace detail {
inline void SetExampleSlot(TacticalPlan& plan, std::size_t index,
                           TacticalAnchor in_possession,
                           TacticalAnchor out_of_possession,
                           PossessionRole possession_role,
                           DefendingRole defending_role) {
  TacticalSlot& slot = plan.slots[index];
  slot.id = static_cast<TacticalSlotId>(index);
  slot.in_possession = in_possession;
  slot.out_of_possession = out_of_possession;
  slot.in_possession_role = possession_role;
  slot.out_of_possession_role = defending_role;
}
}  // namespace detail

// 4-3-3 balanced: the shape the plan bootstrap produces, written out explicitly.
inline TacticalPlan MakeBalanced433() {
  TacticalPlan plan;
  plan.mentality = Mentality::Balanced;
  plan.in_possession = DefaultInPossession(Mentality::Balanced);
  plan.out_of_possession = DefaultOutOfPossession(Mentality::Balanced);
  detail::SetExampleSlot(plan, 0, {0.03f, 0.0f}, {0.03f, 0.0f},
                         PossessionRole::Goalkeeper, DefendingRole::Goalkeeper);
  detail::SetExampleSlot(plan, 1, {0.32f, 0.75f}, {0.28f, 0.65f},
                         PossessionRole::FullBack, DefendingRole::MarkingDefender);
  detail::SetExampleSlot(plan, 2, {0.22f, 0.15f}, {0.18f, 0.12f},
                         PossessionRole::CentreBack, DefendingRole::CoveringDefender);
  detail::SetExampleSlot(plan, 3, {0.22f, -0.15f}, {0.18f, -0.12f},
                         PossessionRole::CentreBack, DefendingRole::CoveringDefender);
  detail::SetExampleSlot(plan, 4, {0.32f, -0.75f}, {0.28f, -0.65f},
                         PossessionRole::FullBack, DefendingRole::MarkingDefender);
  detail::SetExampleSlot(plan, 5, {0.42f, 0.0f}, {0.40f, 0.0f},
                         PossessionRole::HoldingMidfielder,
                         DefendingRole::ScreeningMidfielder);
  detail::SetExampleSlot(plan, 6, {0.55f, 0.20f}, {0.50f, 0.15f},
                         PossessionRole::BoxToBox, DefendingRole::ScreeningMidfielder);
  detail::SetExampleSlot(plan, 7, {0.55f, -0.20f}, {0.50f, -0.15f},
                         PossessionRole::Playmaker, DefendingRole::ScreeningMidfielder);
  detail::SetExampleSlot(plan, 8, {0.78f, 0.80f}, {0.45f, 0.70f},
                         PossessionRole::Winger, DefendingRole::TrackingWinger);
  detail::SetExampleSlot(plan, 9, {0.88f, 0.0f}, {0.60f, 0.0f},
                         PossessionRole::CentreForward, DefendingRole::PressingForward);
  detail::SetExampleSlot(plan, 10, {0.78f, -0.80f}, {0.45f, -0.70f},
                         PossessionRole::InsideForward, DefendingRole::TrackingWinger);
  return plan;
}

// 3-2-5 in possession, 4-4-2 out of possession. The two phases are genuinely
// different shapes; the same eleven slots change anchors and roles.
inline TacticalPlan MakeThreeTwoFiveVsFourFourTwo() {
  TacticalPlan plan;
  plan.mentality = Mentality::Positive;
  plan.in_possession = DefaultInPossession(Mentality::Positive);
  plan.out_of_possession = DefaultOutOfPossession(Mentality::Positive);
  plan.out_of_possession.marking = MarkingStyle::Mixed;
  detail::SetExampleSlot(plan, 0, {0.03f, 0.0f}, {0.03f, 0.0f},
                         PossessionRole::Goalkeeper, DefendingRole::Goalkeeper);
  detail::SetExampleSlot(plan, 1, {0.20f, 0.18f}, {0.30f, 0.70f},
                         PossessionRole::CentreBack, DefendingRole::MarkingDefender);
  detail::SetExampleSlot(plan, 2, {0.20f, 0.0f}, {0.18f, 0.10f},
                         PossessionRole::CentreBack, DefendingRole::CoveringDefender);
  detail::SetExampleSlot(plan, 3, {0.20f, -0.18f}, {0.30f, -0.70f},
                         PossessionRole::CentreBack, DefendingRole::MarkingDefender);
  detail::SetExampleSlot(plan, 4, {0.42f, 0.10f}, {0.42f, 0.20f},
                         PossessionRole::HoldingMidfielder,
                         DefendingRole::ScreeningMidfielder);
  detail::SetExampleSlot(plan, 5, {0.42f, -0.10f}, {0.42f, -0.20f},
                         PossessionRole::Playmaker, DefendingRole::ScreeningMidfielder);
  detail::SetExampleSlot(plan, 6, {0.72f, 0.85f}, {0.40f, 0.75f},
                         PossessionRole::Winger, DefendingRole::TrackingWinger);
  detail::SetExampleSlot(plan, 7, {0.72f, -0.85f}, {0.40f, -0.75f},
                         PossessionRole::Winger, DefendingRole::TrackingWinger);
  detail::SetExampleSlot(plan, 8, {0.72f, 0.0f}, {0.55f, 0.0f},
                         PossessionRole::Playmaker, DefendingRole::PressingMidfielder);
  detail::SetExampleSlot(plan, 9, {0.88f, 0.15f}, {0.60f, 0.15f},
                         PossessionRole::CentreForward, DefendingRole::PressingForward);
  detail::SetExampleSlot(plan, 10, {0.88f, -0.15f}, {0.60f, -0.15f},
                         PossessionRole::InsideForward, DefendingRole::PressingForward);
  return plan;
}

// Low block counter: a deep, compact block that springs forward on a win.
inline TacticalPlan MakeLowBlockCounter() {
  TacticalPlan plan;
  plan.mentality = Mentality::Defensive;
  plan.in_possession = DefaultInPossession(Mentality::Defensive);
  plan.out_of_possession = DefaultOutOfPossession(Mentality::Defensive);
  plan.in_possession.build_up = BuildUpStyle::Direct;
  plan.in_possession.passing_directness = 0.6f;
  plan.out_of_possession.defensive_line = 0.25f;
  plan.out_of_possession.line_of_engagement = 0.28f;
  plan.out_of_possession.pressing_intensity = 0.30f;
  plan.out_of_possession.compactness = 0.78f;
  plan.transition.on_win = TransitionOnWin::Counter;
  plan.transition.on_loss = TransitionOnLoss::Regroup;
  detail::SetExampleSlot(plan, 0, {0.03f, 0.0f}, {0.03f, 0.0f},
                         PossessionRole::Goalkeeper, DefendingRole::Goalkeeper);
  detail::SetExampleSlot(plan, 1, {0.22f, 0.70f}, {0.22f, 0.60f},
                         PossessionRole::FullBack, DefendingRole::MarkingDefender);
  detail::SetExampleSlot(plan, 2, {0.14f, 0.18f}, {0.12f, 0.14f},
                         PossessionRole::CentreBack, DefendingRole::CoveringDefender);
  detail::SetExampleSlot(plan, 3, {0.14f, -0.18f}, {0.12f, -0.14f},
                         PossessionRole::CentreBack, DefendingRole::CoveringDefender);
  detail::SetExampleSlot(plan, 4, {0.22f, -0.70f}, {0.22f, -0.60f},
                         PossessionRole::FullBack, DefendingRole::MarkingDefender);
  detail::SetExampleSlot(plan, 5, {0.38f, 0.12f}, {0.32f, 0.10f},
                         PossessionRole::HoldingMidfielder,
                         DefendingRole::ScreeningMidfielder);
  detail::SetExampleSlot(plan, 6, {0.38f, -0.12f}, {0.32f, -0.10f},
                         PossessionRole::HoldingMidfielder,
                         DefendingRole::ScreeningMidfielder);
  detail::SetExampleSlot(plan, 7, {0.50f, 0.55f}, {0.40f, 0.45f},
                         PossessionRole::Winger, DefendingRole::TrackingWinger);
  detail::SetExampleSlot(plan, 8, {0.50f, -0.55f}, {0.40f, -0.45f},
                         PossessionRole::Winger, DefendingRole::TrackingWinger);
  detail::SetExampleSlot(plan, 9, {0.72f, 0.12f}, {0.48f, 0.10f},
                         PossessionRole::CentreForward, DefendingRole::HoldingForward);
  detail::SetExampleSlot(plan, 10, {0.72f, -0.12f}, {0.48f, -0.10f},
                         PossessionRole::CentreForward, DefendingRole::HoldingForward);
  return plan;
}

}  // namespace football::ai

#endif  // FOOTBALL_AI_TACTICAL_EXAMPLES_HPP
