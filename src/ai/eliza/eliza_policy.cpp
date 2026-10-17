// The Eliza policy loop. It dispatches one PlayerControl per active observed
// player: restart positioning, stand-still gate, keeper anchor, on-ball
// decisions and off-ball movement. P21.5 replaces the keeper branch.
#include "eliza_policy.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <span>

#include "eliza_geometry.hpp"
#include "eliza_intent.hpp"
#include "eliza_observation.hpp"
#include "football/ai/tactical_interpreter.hpp"
#include "football/foundation/math/scalar.hpp"
#include "keeper.hpp"
#include "off_ball.hpp"
#include "on_ball.hpp"

namespace football::ai {
namespace {
using football::math::clamp;
using football::math::Vector3;
using football::player::PlayerId;
using football::sim::TeamSide;
using namespace football::ai::eliza;

const PlayerDirective* FindDirective(const TacticalBoard& board, PlayerId id) {
  for (const PlayerDirective& directive : board.players)
    if (directive.player == id) return &directive;
  return nullptr;
}

// The closest outfield player to the predicted ball interception point. Only
// this actor closes the ball, so the rest of the team keeps its shape.
const WorldPlayerState* Chaser(const WorldState& world, TeamSide side,
                               const Vector3& intercept) {
  const WorldPlayerState* best = nullptr;
  float distance = 0.0f;
  for (const WorldPlayerState& player : world.players) {
    if (!player.active || player.side != side || IsKeeper(player)) continue;
    const float candidate = (player.position - intercept).GetSquaredLength();
    if (best == nullptr || candidate < distance) {
      best = &player;
      distance = candidate;
    }
  }
  return best;
}
}  // namespace

void ElizaPolicy::Reset() {
  possession_ticks_.clear();
}

int ElizaPolicy::PossessionTicks(football::player::PlayerId player) const {
  const auto found = possession_ticks_.find(player);
  return found == possession_ticks_.end() ? 0 : found->second;
}

void ElizaPolicy::Update(const WorldState& world,
                         std::span<const TacticalBoard, 2> tactics,
                         PlayerControlSet& output) {
  output.Clear();
  const Vector3 ball = world.ball_position.Get2D();
  const Vector3 intercept = ball + world.ball_velocity.Get2D() * 0.18f;
  const WorldPlayerState* chasers[2] = {
      Chaser(world, TeamSide::Home, intercept),
      Chaser(world, TeamSide::Away, intercept)};

  // V2 boards are interpreted once per tick into read-only legacy-shaped views
  // plus a bounded intent. Legacy boards keep their exact historical path.
  std::array<TacticalBoard, 2> views = {tactics[0], tactics[1]};
  std::array<ElizaIntent, 2> intents{};
  if (tactics[0].mode == BoardMode::V2 || tactics[1].mode == BoardMode::V2) {
    std::array<SlotAssignment, 2> assignments;
    for (unsigned side = 0; side < 2; ++side)
      assignments[side] = DefaultSlotAssignment(world, static_cast<TeamSide>(side));
    const TacticalInterpretation interpretation = TacticalInterpreter::Build(
        views, world, assignments, InterpretationState::FromWorld(world));
    for (unsigned side = 0; side < 2; ++side) {
      if (interpretation.teams[side].has_plan)
        views[side] = MakeElizaTacticalView(interpretation.teams[side],
                                            interpretation.players[side], world,
                                            static_cast<TeamSide>(side));
      intents[side] = MakeElizaIntent(interpretation.teams[side]);
    }
  }

  for (const WorldPlayerState& player : world.players) {
    if (!player.active) {
      possession_ticks_.erase(player.id);
      continue;
    }
    const unsigned side_index = static_cast<unsigned>(player.side);
    const TacticalBoard& board = views[side_index];
    const ElizaIntent& intent = intents[side_index];
    const PlayerDirective* directive = FindDirective(board, player.id);
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
    // Policy-owned possession clock. It is keyed by PlayerId and advances once
    // per tick; the Player's own decision clock is never touched.
    float possession_ms = 0.0f;
    if (owns_ball) {
      const int ticks = ++possession_ticks_[player.id];
      possession_ms = static_cast<float>(ticks) * 10.0f;
    } else {
      possession_ticks_.erase(player.id);
    }
    const bool keeper = IsKeeper(player);
    ControlAction action = ControlAction::None;
    std::optional<PlayerId> action_target;
    std::optional<Vector3> action_position;
    float action_power = 0.0f;
    Vector3 target = player.position;
    bool uses_dribble_movement = false;
    Vector3 dribble_movement;

    if (keeper) {
      const KeeperDecision decision = DecideKeeper(world, player, parameters_);
      target = decision.target;
      if (decision.save) action = ControlAction::Save;
    } else if (owns_ball || restart_taker) {
      const OnBallDecision decision =
          DecideOnBall(world, board, player, parameters_, possession_ms, intent);
      action = decision.action;
      action_target = decision.target_player;
      action_position = decision.target_position;
      action_power = decision.power;
      dribble_movement = DribbleMovement(world, player, parameters_);
      uses_dribble_movement = true;
    } else {
      const WorldPlayerState* chaser = chasers[static_cast<unsigned>(player.side)];
      if (chaser != nullptr && chaser->id == player.id) {
        const OnBallDecision decision = DecideLooseBall(world, player, parameters_);
        action = decision.action;
        target = intercept;
      } else {
        target = CalculateOffBallTarget(world, board, directive, player, parameters_,
                                       intent);
        action = DecideTackle(world, player);
        if (action == ControlAction::Tackle) target = world.ball_position;
      }
    }

    if (uses_dribble_movement) {
      control.move_direction = SafeNormalized(dribble_movement, player.facing);
      control.desired_speed =
          clamp(dribble_movement.GetLength(), 0.0f, player.max_speed);
    } else {
      const Vector3 delta = (target - player.position).Get2D();
      control.move_direction = SafeNormalized(delta, player.facing);
      control.desired_speed =
          std::min(player.max_speed, delta.GetLength() * parameters_.distance_to_velocity);
    }
    control.action = action;
    control.target_player = action_target;
    control.target_position = action_position;
    control.power = action_power;
    if (owns_ball) control.desired_speed = std::min(control.desired_speed, 5.0f);
    if (world.ball_retainer == player.id) control.desired_speed = 0.0f;
    output.Set(player.id, control);
  }
}

}  // namespace football::ai
