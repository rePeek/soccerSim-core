// The Eliza policy loop. It dispatches one PlayerControl per active observed
// player: restart positioning, stand-still gate, keeper anchor, on-ball
// decisions and off-ball movement. P21.5 replaces the keeper branch.
#include "eliza_policy.hpp"

#include <algorithm>
#include <optional>
#include <span>

#include "eliza_geometry.hpp"
#include "eliza_observation.hpp"
#include "football/foundation/math/scalar.hpp"
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

void ElizaPolicy::Update(const WorldState& world,
                         std::span<const TacticalBoard, 2> tactics,
                         PlayerControlSet& output) {
  output.Clear();
  const Vector3 ball = world.ball_position.Get2D();
  const Vector3 intercept = ball + world.ball_velocity.Get2D() * 0.18f;
  const WorldPlayerState* chasers[2] = {
      Chaser(world, TeamSide::Home, intercept),
      Chaser(world, TeamSide::Away, intercept)};

  for (const WorldPlayerState& player : world.players) {
    if (!player.active) continue;
    const TacticalBoard& board = tactics[static_cast<unsigned>(player.side)];
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
    const bool keeper = IsKeeper(player);
    ControlAction action = ControlAction::None;
    std::optional<PlayerId> action_target;
    std::optional<Vector3> action_position;
    float action_power = 0.0f;
    Vector3 target = player.position;
    bool uses_dribble_movement = false;
    Vector3 dribble_movement;

    if (keeper) {
      // P21.5 adds rush and save logic; P21.4 keeps the keeper on its anchor.
      target = CalculateFormationAnchor(world, board, directive, player);
    } else if (owns_ball || restart_taker) {
      const OnBallDecision decision = DecideOnBall(world, board, player, parameters_);
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
        target = CalculateOffBallTarget(world, board, directive, player, parameters_);
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
