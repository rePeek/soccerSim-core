#include "ai/default_ai.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>

namespace football::ai {
namespace {
using blunted::Vector3;
using model::PlayerId;
using model::TeamSide;

const WorldPlayerState *Find(const WorldState &world, PlayerId id) {
  for (const auto &player : world.players) if (player.id == id) return &player;
  return nullptr;
}

const PlayerDirective *Directive(std::span<const TacticalBoard> boards,
                                 TeamSide side, PlayerId id) {
  const auto &board = boards[static_cast<unsigned>(side)];
  for (const auto &directive : board.players)
    if (directive.player == id) return &directive;
  return nullptr;
}

// Ties follow observation/roster order, never IDs or scheduler phases.
const WorldPlayerState *Closest(const WorldState &world, TeamSide side,
                               const Vector3 &position, bool exclude_keeper,
                               std::span<const TacticalBoard> boards) {
  const WorldPlayerState *best = nullptr;
  float distance = std::numeric_limits<float>::max();
  for (const auto &player : world.players) {
    const auto *directive = Directive(boards, player.side, player.id);
    if (!player.active || player.side != side || player.lazy ||
        (exclude_keeper && directive && directive->role == PlannedPlayerRole::Goalkeeper)) continue;
    const float candidate = (player.position - position).GetSquaredLength();
    if (candidate < distance) { best = &player; distance = candidate; }
  }
  return best;
}

bool TeamHasBall(const WorldState &world, TeamSide side) {
  for (const auto &player : world.players)
    if (player.active && player.side == side && player.has_possession) return true;
  return false;
}

// Compute this tick's target from immutable intent and actual match state.
Vector3 FormationTarget(const WorldState &world, const TacticalBoard &board,
                        const PlayerDirective *directive, const WorldPlayerState &player) {
  if (!directive || !directive->formation_position) return player.position;
  const int defend = world.teams[static_cast<unsigned>(player.side)].defending_direction;
  if (directive->role == PlannedPlayerRole::Goalkeeper)
    return Vector3(defend * (world.pitch.half_length() - 2.f),
                   std::clamp(world.ball_position.coords[1] * 0.2f, -3.f, 3.f), 0);
  Vector3 position = *directive->formation_position;
  const float width = board.width > 0.f ? board.width : world.pitch.width() * 0.75f;
  const float depth = board.depth > 0.f ? board.depth : world.pitch.length() * 0.55f;
  position.coords[0] *= depth / (world.pitch.length() * 0.55f);
  position.coords[1] *= width / (world.pitch.width() * 0.75f);
  const auto marking = player.pressure_remaining_ms > 0 && player.marking_target
      ? player.marking_target : directive->marking_target;
  if (marking) {
    const auto *opponent = Find(world, *marking);
    if (opponent && opponent->active && opponent->side != player.side)
      position = opponent->position + Vector3(defend * 2.f, 0, 0);
  }
  const float attack = static_cast<float>(-defend);
  position.coords[0] += world.ball_position.coords[0] * 0.2f +
                        attack * (TeamHasBall(world, player.side) ? 8.f : -3.f);
  position.coords[1] += world.ball_position.coords[1] * 0.15f;
  if (player.attacking_run_remaining_ms > 0) position.coords[0] += attack * 15.f;
  position.coords[0] = std::clamp(position.coords[0], -world.pitch.half_length() + 2.f,
                                 world.pitch.half_length() - 2.f);
  position.coords[1] = std::clamp(position.coords[1], -world.pitch.half_width() + 2.f,
                                 world.pitch.half_width() - 2.f);
  return position;
}
}  // namespace

void DefaultAI::Update(const WorldState &world, PlayerControlSet &output) const {
  const std::span<const TacticalBoard> boards = boards_;
  output.Clear();
  const Vector3 ball = world.ball_position.Get2D();
  const Vector3 intercept = ball + world.ball_velocity.Get2D() * 0.18f;
  for (const auto &player : world.players) {
    if (!player.active || player.externally_controlled) continue;
    PlayerControl control;
    control.player = player.id;
    control.look_at = ball;
    const auto *directive = Directive(boards, player.side, player.id);
    const bool keeper = directive && directive->role == PlannedPlayerRole::Goalkeeper;
    const int defend = world.teams[static_cast<unsigned>(player.side)].defending_direction;
    const Vector3 goal(-defend * world.pitch.half_length(), 0, 0);
    Vector3 target = FormationTarget(world, tactics(player.side), directive, player);

    if (!world.in_play || player.lazy ||
        (world.in_set_piece && world.restart_taker != player.id)) {
      control.move_direction = player.facing;
      output.Set(player.id, control);
      continue;
    }
    const auto *chaser = Closest(world, player.side, intercept, true, boards);
    if ((chaser && chaser->id == player.id) || player.pressure_remaining_ms > 0) target = intercept;
    if (keeper && (ball.coords[0] * defend > world.pitch.half_length() - 16.f ||
                   player.keeper_rush_remaining_ms > 0)) target = intercept;

    const bool owns_ball = player.has_possession || world.ball_retainer == player.id;
    const bool restart_taker = world.in_set_piece && world.restart_taker == player.id;
    if (owns_ball || restart_taker) {
      target = goal;
      const float goal_distance = (goal - player.position).GetLength();
      if ((!keeper && goal_distance < 24.f) || world.restart == e_GameMode_Penalty) {
        control.action = ControlAction::Shoot;
        control.target_position = goal;
        control.power = 0.8f;
      } else {
        const WorldPlayerState *recipient = nullptr;
        float best = -std::numeric_limits<float>::max();
        for (const auto &mate : world.players) {
          if (!mate.active || mate.side != player.side || mate.id == player.id) continue;
          const float distance = (mate.position - player.position).GetLength();
          if (distance < 3.f || distance > 38.f) continue;
          float space = 20.f;
          for (const auto &opponent : world.players) {
            if (opponent.active && opponent.side != player.side)
              space = std::min(space, (opponent.position - mate.position).GetLength());
          }
          const float progress = (mate.position.coords[0] - player.position.coords[0]) * -defend;
          const float rating = progress + space * 0.6f - distance * 0.1f;
          if (rating > best) { best = rating; recipient = &mate; }
        }
        if (recipient && (best > 5.f || restart_taker || keeper)) {
          control.action = ControlAction::ShortPass;
          control.target_player = recipient->id;
          control.target_position = recipient->position + recipient->velocity * 0.2f;
          control.power = std::clamp((recipient->position - player.position).GetLength() / 45.f, 0.15f, 0.8f);
        } else if (restart_taker || keeper) {
          // A one-player roster must still be able to release a restart/retain.
          control.action = world.restart == e_GameMode_ThrowIn
              ? ControlAction::ShortPass : ControlAction::HighPass;
          control.target_position = goal;
          control.power = 0.6f;
        }
      }
    }
    if (control.action == ControlAction::None && !world.ball_retainer) {
      if (owns_ball || ((chaser && chaser->id == player.id) &&
                       (ball - player.position).GetLength() < 2.f &&
                       world.ball_position.coords[2] < 1.5f))
        control.action = world.ball_velocity.GetLength() > 6.f && !owns_ball
            ? ControlAction::Trap : ControlAction::Dribble;
      if (keeper && (ball - player.position).GetLength() < 3.f)
        control.action = ControlAction::Save;
    }
    if (restart_taker && !world.ball_retainer) target = ball;
    const Vector3 delta = (target - player.position).Get2D();
    control.move_direction = delta.GetNormalized(player.facing);
    control.desired_speed = std::min(player.max_speed, delta.GetLength() * 2.f);
    if (owns_ball) control.desired_speed = std::min(control.desired_speed, 5.f);
    if (restart_taker) control.desired_speed = delta.GetLength() > 0.8f
        ? std::min(player.max_speed, 3.f) : 0.f;
    if (world.ball_retainer == player.id) control.desired_speed = 0.f;
    output.Set(player.id, control);
  }
}
}  // namespace football::ai
