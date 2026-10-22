#include "heuristic_policy.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include "../eliza/eliza_geometry.hpp"
#include "../eliza/eliza_intent.hpp"
#include "../eliza/eliza_observation.hpp"
#include "../eliza/keeper.hpp"
#include "../eliza/on_ball.hpp"
#include "football/ai/tactical_interpreter.hpp"

namespace football::ai {
namespace {
using football::math::clamp;
using football::math::Vector3;
using football::player::PlayerId;
using football::sim::TeamSide;
using namespace football::ai::eliza;

const WorldPlayerState* ClosestTo(const WorldState& world, TeamSide side,
                                  const Vector3& point, bool exclude_keeper,
                                  PlayerId exclude = football::player::kInvalidPlayerId) {
  const WorldPlayerState* best = nullptr;
  float distance = 0.0f;
  for (const WorldPlayerState& player : world.players) {
    if (!player.active || player.side != side || player.id == exclude) continue;
    if (exclude_keeper && IsKeeper(player)) continue;
    const float candidate = (player.position - point).GetSquaredLength();
    if (best == nullptr || candidate < distance) {
      best = &player;
      distance = candidate;
    }
  }
  return best;
}

float AttackSign(const WorldState& world, TeamSide side) {
  return -static_cast<float>(world.teams[static_cast<unsigned>(side)].defending_direction);
}

// Canonical depth (0 own goal .. 1 opponent goal) of a world target.
float DepthU(const WorldState& world, TeamSide side, const Vector3& point) {
  return 0.5f + point.coords[0] * AttackSign(world, side) / world.pitch.length();
}

Vector3 ClampToPitch(const WorldState& world, Vector3 point) {
  point.coords[0] =
      clamp(point.coords[0], -world.pitch.half_length() + 2.0f,
            world.pitch.half_length() - 2.0f);
  point.coords[1] =
      clamp(point.coords[1], -world.pitch.half_width() + 2.0f,
            world.pitch.half_width() - 2.0f);
  return point;
}

struct SidePlan {
  const WorldPlayerState* holder = nullptr;
  const WorldPlayerState* chaser = nullptr;
  const WorldPlayerState* presser = nullptr;
  const WorldPlayerState* cover = nullptr;
  bool attacking = false;
  std::vector<PlayerId> rest_defence;
};

}  // namespace

void HeuristicPolicy::Reset() {
  for (auto& assignment : assignments_) assignment.clear();
  possession_ticks_.clear();
  smoothed_targets_.clear();
}

int HeuristicPolicy::PossessionTicks(PlayerId player) const {
  const auto found = possession_ticks_.find(player);
  return found == possession_ticks_.end() ? 0 : found->second;
}

void HeuristicPolicy::Update(const PlayerAIContext& context,
                             PlayerControlSet& output) {
  const WorldState& world = *context.world;
  const std::span<const TacticalBoard> tactics = context.tactics;
  output.Clear();
  for (auto& assignment : assignments_) assignment.clear();

  const ElizaParameters parameters;
  const Vector3 ball = world.ball_position.Get2D();
  const Vector3 intercept = ball + world.ball_velocity.Get2D() * 0.18f;

  // Interpret the persistent plan once per tick: the plan is the only
  // authority for role, anchor and team instruction.
  std::array<ElizaIntent, 2> intents{};
  std::array<std::array<EffectivePlayerTactics, kTacticalSlotCount>, 2> effective{};
  {
    std::array<SlotAssignment, 2> slot_assignments;
    for (unsigned side = 0; side < 2; ++side)
      slot_assignments[side] =
          DefaultSlotAssignment(world, static_cast<TeamSide>(side));
    const TacticalInterpretation interpretation = TacticalInterpreter::Build(
        tactics, world, slot_assignments, InterpretationState::FromWorld(world));
    for (unsigned side = 0; side < 2; ++side) {
      effective[side] = interpretation.players[side];
      intents[side] = MakeElizaIntent(interpretation.teams[side]);
    }
  }

  // ---- Team task assignment (one joint plan per side, from one snapshot) ----
  std::array<SidePlan, 2> plans{};
  for (unsigned index = 0; index < 2; ++index) {
    const auto side = static_cast<TeamSide>(index);
    SidePlan& plan = plans[index];
    plan.attacking = TeamOf(world, side).has_possession;
    plan.holder = BallHolder(world, side);
    plan.chaser = ClosestTo(world, side, intercept, true);
    plan.presser = ClosestTo(world, side, ball, true);
    plan.cover = ClosestTo(world, side, ball, true,
                             plan.presser ? plan.presser->id
                                           : football::player::kInvalidPlayerId);

    if (plan.attacking) {
      // Rest defence: the two deepest outfielders hold behind the ball.
      std::vector<std::pair<float, PlayerId>> depth;
      for (const WorldPlayerState& player : world.players) {
        if (!player.active || player.side != side || IsKeeper(player)) continue;
        const EffectivePlayerTactics* eff = FindEffective(effective[index], player.id);
        const Vector3 point = eff ? eff->target_anchor_metres : player.position;
        depth.emplace_back(DepthU(world, side, point), player.id);
      }
      std::sort(depth.begin(), depth.end(),
                [](const auto& a, const auto& b) { return a.first < b.first; });
      for (std::size_t i = 0; i < depth.size() && i < 2; ++i)
        plan.rest_defence.push_back(depth[i].second);
    }
  }

  for (const WorldPlayerState& player : world.players) {
    if (!player.active) {
      possession_ticks_.erase(player.id);
      smoothed_targets_.erase(player.id);
      continue;
    }
    const unsigned index = static_cast<unsigned>(player.side);
    const auto side = player.side;
    const ElizaIntent& intent = intents[index];
    const SidePlan& plan = plans[index];
    const EffectivePlayerTactics* eff = FindEffective(effective[index], player.id);
    const float attack = AttackSign(world, side);
    const float defend = -attack;

    PlayerControl control;
    control.player = player.id;
    control.look_at = world.ball_position;

    if (world.restart_pending) {
      const Vector3 delta =
          (player.restart_target.value_or(player.position) - player.position).Get2D();
      control.move_direction = SafeNormalized(delta, player.facing);
      control.desired_speed = std::min(player.max_speed, delta.GetLength() * 2.0f);
      output.Set(player.id, control);
      continue;
    }
    const bool restart_taker = world.in_set_piece && world.restart_taker == player.id;
    if (!world.in_play || (player.lazy && !restart_taker) ||
        (world.in_set_piece && !restart_taker)) {
      control.move_direction = player.facing;
      output.Set(player.id, control);
      continue;
    }

    const bool owns_ball = player.has_possession || world.ball_retainer == player.id;
    float possession_ms = 0.0f;
    if (owns_ball) {
      const int ticks = ++possession_ticks_[player.id];
      possession_ms = static_cast<float>(ticks) * 10.0f;
    } else {
      possession_ticks_.erase(player.id);
    }

    // ---- Coordinated off-ball target ----
    Vector3 raw = player.position;
    if (eff != nullptr) raw = eff->target_anchor_metres;
    raw.coords[0] += world.ball_position.coords[0] * 0.15f;
    raw.coords[1] += world.ball_position.coords[1] * 0.12f;

    // Transient mental effects tilt tendencies. They never change base
    // abilities, the observed world or the persistent plan.
    const PlayerMentalEffect mental =
        context.MentalEffect(side, player.id);
    if (mental.motivation != 0.0f)
      raw.coords[0] += attack * 2.0f * mental.motivation;
    ElizaIntent player_intent = intent;
    if (mental.motivation != 0.0f || mental.confidence != 0.0f ||
        mental.focus != 0.0f || mental.composure != 0.0f) {
      // The plan intent is always active; the transient tilt is bounded on top.
      player_intent.risk_taking =
          clamp(player_intent.risk_taking + 0.15f * mental.confidence -
                    0.20f * mental.composure,
                0.0f, 1.0f);
      player_intent.tempo =
          clamp(player_intent.tempo + 0.15f * mental.motivation, 0.0f, 1.0f);
    }
    // Bounded decision tendencies: team/role/individual from the interpreter
    // plus the transient mental tilt. Recorded for diagnostics and used below.
    float forward_run_bias = eff != nullptr ? eff->forward_run_bias : 0.5f;
    float pass_risk = eff != nullptr ? eff->pass_risk : 0.5f;
    float press_bias = eff != nullptr ? eff->press_bias : 0.5f;
    if (mental.motivation != 0.0f) {
      forward_run_bias = clamp(forward_run_bias + 0.25f * mental.motivation, 0.0f, 1.0f);
      press_bias = clamp(press_bias + 0.10f * mental.motivation, 0.0f, 1.0f);
    }
    if (mental.confidence != 0.0f)
      press_bias = clamp(press_bias + 0.10f * mental.confidence, 0.0f, 1.0f);
    if (mental.focus != 0.0f)
      press_bias = clamp(press_bias + 0.20f * mental.focus, 0.0f, 1.0f);
    if (mental.composure != 0.0f)
      pass_risk = clamp(pass_risk - 0.20f * mental.composure, 0.0f, 1.0f);

    bool is_presser = false;
    bool is_cover = false;
    bool is_marking = false;
    bool is_rest = false;
    bool is_holder = plan.holder != nullptr && plan.holder->id == player.id;
    const bool keeper = IsKeeper(player);

    if (!keeper && !is_holder) {
      if (plan.attacking) {
        if (std::find(plan.rest_defence.begin(), plan.rest_defence.end(),
                      player.id) != plan.rest_defence.end()) {
          // Hold behind the ball, never beyond it.
          const float ball_front = world.ball_position.coords[0] * attack;
          float mine = raw.coords[0] * attack;
          if (mine > ball_front - 4.0f) mine = ball_front - 4.0f;
          raw.coords[0] = mine * attack;
          is_rest = true;
        }
      } else {
        if (plan.presser != nullptr && plan.presser->id == player.id) {
          raw = intercept;
          is_presser = true;
        } else if (plan.cover != nullptr && plan.cover->id == player.id) {
          const Vector3 own_goal(defend * world.pitch.half_length(), 0.0f, 0.0f);
          raw = ball * 0.45f + own_goal * 0.55f;
          is_cover = true;
        }
        if (!is_presser && eff != nullptr && eff->mark_target) {
          const WorldPlayerState* opponent = FindPlayer(world, *eff->mark_target);
          if (opponent != nullptr && opponent->active && opponent->side != side) {
            raw = opponent->position + Vector3(defend * 2.0f, 0.0f, 0.0f);
            is_marking = true;
          }
        }
      }
    }

    // Hysteresis for plain off-ball movement only; press/cover/holder must stay
    // responsive to the live ball.
    Vector3 target = raw;
    const bool smooth = !keeper && !is_holder && !is_presser && !is_cover;
    if (smooth) {
      const auto found = smoothed_targets_.find(player.id);
      if (found != smoothed_targets_.end())
        target = found->second + (raw - found->second) * 0.4f;
      smoothed_targets_[player.id] = target;
    } else {
      smoothed_targets_.erase(player.id);
    }

    if (plan.attacking) {
      const float offside_x = OffsideLineX(world, side);
      target.coords[0] =
          ApplyOffsideConstraint(target.coords[0], offside_x, attack, 0.05f,
                                 world.pitch.half_length());
    }
    target = ClampToPitch(world, target);

    // ---- Discrete action intent ----
    ControlAction action = ControlAction::None;
    std::optional<PlayerId> action_target;
    std::optional<Vector3> action_position;
    float action_power = 0.0f;
    Vector3 move_target = target;
    bool uses_dribble_movement = false;
    Vector3 dribble_movement;

    if (keeper) {
      const KeeperDecision decision = DecideKeeper(world, player, parameters);
      move_target = decision.target;
      if (decision.save) action = ControlAction::Save;
    } else if (owns_ball || restart_taker) {
      const OnBallDecision decision =
          DecideOnBall(world, player, parameters, possession_ms,
                       player_intent);
      action = decision.action;
      action_target = decision.target_player;
      action_position = decision.target_position;
      action_power = decision.power;
      dribble_movement = DribbleMovement(world, player, parameters);
      uses_dribble_movement = true;
    } else if (plan.chaser != nullptr && plan.chaser->id == player.id &&
               !world.teams[index].has_possession) {
      const OnBallDecision decision = DecideLooseBall(world, player, parameters);
      action = decision.action;
      move_target = intercept;
    } else {
      action = DecideTackle(world, player);
      if (action == ControlAction::Tackle) move_target = world.ball_position;
    }

    if (uses_dribble_movement) {
      control.move_direction = SafeNormalized(dribble_movement, player.facing);
      control.desired_speed =
          clamp(dribble_movement.GetLength(), 0.0f, player.max_speed);
    } else {
      const Vector3 delta = (move_target - player.position).Get2D();
      control.move_direction = SafeNormalized(delta, player.facing);
      control.desired_speed =
          std::min(player.max_speed, delta.GetLength() * parameters.distance_to_velocity);
    }
    control.action = action;
    control.target_player = action_target;
    control.target_position = action_position;
    control.power = action_power;
    if (owns_ball) control.desired_speed = std::min(control.desired_speed, 5.0f);
    if (world.ball_retainer == player.id) control.desired_speed = 0.0f;
    output.Set(player.id, control);

    HeuristicAssignment assignment;
    assignment.player = player.id;
    assignment.target = target;
    assignment.holder = is_holder || owns_ball;
    assignment.presser = is_presser;
    assignment.cover = is_cover;
    assignment.marking = is_marking;
    assignment.rest_defence = is_rest;
    assignment.forward_run_bias = forward_run_bias;
    assignment.pass_risk = pass_risk;
    assignment.press_bias = press_bias;
    assignments_[index].push_back(assignment);
  }
}

}  // namespace football::ai
