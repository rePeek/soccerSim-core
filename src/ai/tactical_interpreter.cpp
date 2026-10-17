#include "football/ai/tactical_interpreter.hpp"

#include <algorithm>
#include <cmath>

namespace football::ai {
namespace {
using football::math::Vector3;
using football::sim::TeamSide;

constexpr float Clamp01(float value) {
  return std::clamp(value, 0.0f, 1.0f);
}

constexpr bool Finite(float value) { return std::isfinite(value); }

// Role-shaped bounded offsets applied on top of the team instruction. Roles
// never replace the team instruction; they shift it within a small range.
struct RoleProfile {
  float forward_bias = 0.0f;
  float pass_risk = 0.0f;
  float press = 0.0f;
  float dribble = 0.0f;
  float freedom = 0.0f;
  float depth_offset = 0.0f;   // +u = further forward
  float width_offset = 0.0f;   // +v = wider
  float line_follow = 0.4f;    // how strongly the unit follows defensive_line
  bool presser = false;
};

RoleProfile InPossessionProfile(PossessionRole role) {
  switch (role) {
    case PossessionRole::Goalkeeper:
      return {.press = -0.2f, .freedom = -0.2f, .line_follow = 0.1f};
    case PossessionRole::CentreBack:
      return {.pass_risk = -0.1f, .freedom = -0.15f, .depth_offset = -0.06f};
    case PossessionRole::FullBack:
      return {.forward_bias = 0.05f, .depth_offset = 0.04f, .width_offset = 0.1f};
    case PossessionRole::InvertedFullBack:
      return {.forward_bias = 0.05f, .freedom = 0.05f, .width_offset = -0.35f};
    case PossessionRole::HoldingMidfielder:
      return {.pass_risk = -0.1f, .freedom = -0.05f, .line_follow = 0.5f};
    case PossessionRole::Playmaker:
      return {.pass_risk = 0.1f, .freedom = 0.15f, .line_follow = 0.5f};
    case PossessionRole::BoxToBox:
      return {.forward_bias = 0.08f, .depth_offset = 0.05f};
    case PossessionRole::Winger:
      return {.forward_bias = 0.1f, .dribble = 0.15f, .freedom = 0.1f,
              .depth_offset = 0.05f, .width_offset = 0.15f};
    case PossessionRole::InsideForward:
      return {.forward_bias = 0.15f, .dribble = 0.1f, .depth_offset = 0.07f,
              .width_offset = -0.25f};
    case PossessionRole::CentreForward:
      return {.forward_bias = 0.15f, .depth_offset = 0.08f, .line_follow = 0.1f};
    case PossessionRole::FalseNine:
      return {.forward_bias = 0.1f, .freedom = 0.15f, .depth_offset = -0.05f,
              .line_follow = 0.1f};
  }
  return {};
}

RoleProfile OutOfPossessionProfile(DefendingRole role) {
  switch (role) {
    case DefendingRole::Goalkeeper:
      return {.press = -0.3f, .line_follow = 0.05f};
    case DefendingRole::CoveringDefender:
      return {.depth_offset = -0.05f, .line_follow = 0.7f};
    case DefendingRole::MarkingDefender:
      return {.width_offset = 0.1f, .line_follow = 0.65f};
    case DefendingRole::ScreeningMidfielder:
      return {.freedom = -0.05f, .line_follow = 0.5f};
    case DefendingRole::PressingMidfielder:
      return {.press = 0.2f, .line_follow = 0.4f, .presser = true};
    case DefendingRole::TrackingWinger:
      return {.width_offset = 0.15f, .line_follow = 0.3f, .presser = true};
    case DefendingRole::PressingForward:
      return {.press = 0.25f, .depth_offset = 0.05f, .line_follow = 0.1f,
              .presser = true};
    case DefendingRole::HoldingForward:
      return {.depth_offset = -0.08f, .line_follow = 0.1f};
  }
  return {};
}

float WideningScale(float attacking_width) {
  // 0 -> narrow (0.65), 0.75 default -> ~1.03, 1 -> wide (1.15).
  return 0.65f + 0.5f * Clamp01(attacking_width);
}

std::optional<football::player::PlayerId> PlanMarkTarget(
    const TacticalSlot& slot) {
  if (!slot.instructions.marking_target) return std::nullopt;
  if (*slot.instructions.marking_target == football::player::kInvalidPlayerId)
    return std::nullopt;
  return slot.instructions.marking_target;
}

}  // namespace

Vector3 TacticalAnchorToWorld(const TacticalAnchor& anchor,
                              int defending_direction,
                              const football::pitch::Pitch& pitch) {
  // attack_sign points from the own goal towards the opponent goal.
  const float attack_sign = -static_cast<float>(defending_direction);
  const float x = attack_sign * (anchor.u - 0.5f) * pitch.length();
  const float y = attack_sign * anchor.v * pitch.half_width();
  return Vector3(x, y, 0.0f);
}

TacticalAnchor WorldPositionToAnchor(const Vector3& position,
                                     int defending_direction,
                                     const football::pitch::Pitch& pitch) {
  const float attack_sign = -static_cast<float>(defending_direction);
  TacticalAnchor anchor;
  anchor.u = position.coords[0] * attack_sign / pitch.length() + 0.5f;
  anchor.v = position.coords[1] * attack_sign / pitch.half_width();
  return anchor;
}

TacticalPhase PhaseFromPossession(bool team_has_possession) {
  return team_has_possession ? TacticalPhase::InPossession
                             : TacticalPhase::OutOfPossession;
}

InterpretationState InterpretationState::FromWorld(const WorldState& world) {
  InterpretationState state;
  for (std::size_t i = 0; i < state.phase.size(); ++i) {
    const auto& team = world.teams[i];
    state.phase[i] = PhaseFromPossession(team.has_possession);
  }
  return state;
}

SlotAssignment DefaultSlotAssignment(const WorldState& world,
                                     football::sim::TeamSide side) {
  SlotAssignment assignment;
  std::size_t slot = 0;
  for (const WorldPlayerState& player : world.players) {
    if (slot >= kTacticalSlotCount) break;
    if (!player.active || player.side != side) continue;
    assignment.players[slot++] = player.id;
  }
  return assignment;
}

TacticalInterpretation TacticalInterpreter::Build(
    const std::array<TacticalBoard, 2>& boards, const WorldState& world,
    const std::array<SlotAssignment, 2>& assignments,
    const InterpretationState& state) {
  TacticalInterpretation result;
  for (std::size_t side_index = 0; side_index < 2; ++side_index) {
    EffectiveTeamTactics& team = result.teams[side_index];
    const TacticalPlanV2* plan = AdvancedPlan(boards[side_index]);
    if (!plan) {
      team.has_plan = false;
      continue;
    }
    team.has_plan = true;
    team.phase = state.phase[side_index];
    team.transition_momentum =
        Finite(state.transition_momentum[side_index])
            ? std::clamp(state.transition_momentum[side_index], -1.0f, 1.0f)
            : 0.0f;

    const InPossessionInstructions& ip = plan->in_possession;
    const OutOfPossessionInstructions& oop = plan->out_of_possession;
    const TransitionInstructions& transition = plan->transition;

    team.tempo = ip.tempo;
    team.risk = ip.risk_taking;
    team.press = oop.pressing_intensity;
    team.defensive_line = oop.defensive_line;
    team.engagement_line = oop.line_of_engagement;
    team.width = ip.attacking_width;
    team.compactness = oop.compactness;
    team.on_win = transition.on_win;
    team.on_loss = transition.on_loss;
    team.marking = oop.marking;
    team.offside_trap = oop.offside_trap;

    const int defending_direction = world.teams[side_index].defending_direction;
    const bool man_marking = oop.marking != MarkingStyle::Zonal;

    for (std::size_t i = 0; i < plan->slots.size(); ++i) {
      const TacticalSlot& slot = plan->slots[i];
      EffectivePlayerTactics& effective = result.players[side_index][i];
      effective.slot = slot.id;
      if (assignments[side_index].players[i]) {
        effective.player = *assignments[side_index].players[i];
        effective.assigned = true;
      }

      const bool in_possession = team.phase == TacticalPhase::InPossession;
      const TacticalAnchor& base =
          in_possession ? slot.in_possession : slot.out_of_possession;
      const RoleProfile profile = in_possession
          ? InPossessionProfile(slot.in_possession_role)
          : OutOfPossessionProfile(slot.out_of_possession_role);

      float u = base.u;
      float v = base.v;
      float forward_bias = 0.5f;
      float pass_risk = 0.5f;
      float press_bias = 0.5f;
      float dribble_bias = 0.5f;
      float freedom = 0.5f;

      if (in_possession) {
        forward_bias = ip.attacking_bias + profile.forward_bias;
        pass_risk = ip.risk_taking + profile.pass_risk;
        press_bias = 0.35f + 0.4f * ip.attacking_bias + profile.press;
        dribble_bias = ip.creative_freedom * 0.5f + profile.dribble;
        freedom = ip.creative_freedom + profile.freedom;
        u += 0.18f * (ip.attacking_bias - 0.5f) + profile.depth_offset;
        v = v * WideningScale(ip.attacking_width) +
            std::clamp(v * 4.0f, -1.0f, 1.0f) * profile.width_offset;
        if (team.on_win == TransitionOnWin::Counter && team.transition_momentum > 0)
          forward_bias += 0.15f * team.transition_momentum;
        if (team.on_win == TransitionOnWin::HoldShape && team.transition_momentum > 0) {
          pass_risk -= 0.15f * team.transition_momentum;
          forward_bias -= 0.1f * team.transition_momentum;
        }
      } else {
        forward_bias = ip.attacking_bias * 0.4f + profile.forward_bias;
        pass_risk = ip.risk_taking * 0.6f + profile.pass_risk;
        press_bias = oop.pressing_intensity + profile.press;
        dribble_bias = ip.creative_freedom * 0.3f + profile.dribble;
        freedom = ip.creative_freedom * 0.6f + profile.freedom;
        if (profile.presser) {
          u = 0.3f + 0.55f * oop.line_of_engagement + profile.depth_offset;
        } else {
          u += (oop.defensive_line - 0.5f) * profile.line_follow +
               profile.depth_offset;
        }
        v = v * (1.0f - 0.35f * oop.compactness) +
            std::clamp(v * 4.0f, -1.0f, 1.0f) * profile.width_offset;
        if (team.on_loss == TransitionOnLoss::CounterPress &&
            team.transition_momentum < 0)
          press_bias -= 0.2f * team.transition_momentum;
        if (team.on_loss == TransitionOnLoss::Regroup &&
            team.transition_momentum < 0) {
          press_bias += 0.2f * team.transition_momentum;
          u -= 0.1f * -team.transition_momentum;
        }
      }

      // Individual instructions are the final per-player override. Explicit
      // values win over role and team defaults; the team instruction itself is
      // never rewritten.
      const IndividualInstructions& individual = slot.instructions;
      if (individual.forward_runs) forward_bias = *individual.forward_runs;
      if (individual.pressing) press_bias = *individual.pressing;
      if (individual.passing_risk) pass_risk = *individual.passing_risk;
      if (individual.dribbling) dribble_bias = *individual.dribbling;
      if (individual.positional_freedom)
        freedom = *individual.positional_freedom;

      TacticalAnchor effective_anchor;
      effective_anchor.u = Clamp01(u);
      effective_anchor.v = std::clamp(v, -1.0f, 1.0f);
      effective.target_anchor_metres =
          TacticalAnchorToWorld(effective_anchor, defending_direction,
                                world.pitch);
      effective.forward_run_bias = Clamp01(forward_bias);
      effective.pass_risk = Clamp01(pass_risk);
      effective.press_bias = Clamp01(press_bias);
      effective.dribble_bias = Clamp01(dribble_bias);
      effective.positional_freedom = Clamp01(freedom);

      // Man-marking is only requested when the marking style allows it. The
      // target is a plan intent; the Player AI decides how to shadow it.
      if (man_marking) effective.mark_target = PlanMarkTarget(slot);

      if (!Finite(effective.target_anchor_metres.coords[0]) ||
          !Finite(effective.target_anchor_metres.coords[1])) {
        effective.target_anchor_metres = world.ball_position.Get2D();
      }
    }

    // Exclusive marking: the same opponent must not be assigned twice.
    for (std::size_t a = 0; a < result.players[side_index].size(); ++a) {
      const auto& left = result.players[side_index][a];
      if (!left.assigned || !left.mark_target) continue;
      for (std::size_t b = a + 1; b < result.players[side_index].size(); ++b) {
        const auto& right = result.players[side_index][b];
        if (right.assigned && right.mark_target &&
            *left.mark_target == *right.mark_target) {
          team.marking_conflict = true;
        }
      }
    }
  }
  return result;
}

}  // namespace football::ai
