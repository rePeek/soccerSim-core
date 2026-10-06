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
                               std::span<const TacticalBoard> boards,
                               std::optional<PlayerId> excluded = std::nullopt) {
  const WorldPlayerState *best = nullptr;
  float distance = std::numeric_limits<float>::max();
  for (const auto &player : world.players) {
    const auto *directive = Directive(boards, player.side, player.id);
    if (!player.active || player.side != side || player.lazy || player.id == excluded ||
        (exclude_keeper && directive && directive->role == PlannedPlayerRole::Goalkeeper)) continue;
    const float candidate = (player.position - position).GetSquaredLength();
    if (candidate < distance) { best = &player; distance = candidate; }
  }
  return best;
}

const WorldPlayerState *BallOwner(const WorldState &world, TeamSide side,
                                 std::span<const TacticalBoard> boards) {
  if (world.ball_retainer) {
    const auto *retainer = Find(world, *world.ball_retainer);
    if (retainer && retainer->active && retainer->side == side) return retainer;
  }
  for (const auto &player : world.players)
    if (player.active && player.side == side && player.has_possession) return &player;
  return Closest(world, side, world.ball_position.Get2D(), false, boards);
}

bool TeamHasBall(const WorldState &world, TeamSide side) {
  for (const auto &player : world.players)
    if (player.active && player.side == side && player.has_possession) return true;
  return false;
}

bool Requested(const TimedPlayerIntent &intent, const WorldState &world,
               const WorldPlayerState &player) {
  if (!world.in_play || world.in_set_piece || !player.active ||
      intent.player != player.id || !intent.Active(world.tick, world.reset_sequence)) return false;
  if (intent.marking_target) {
    const auto *opponent = Find(world, *intent.marking_target);
    if (!opponent || !opponent->active || opponent->side == player.side) return false;
  }
  return true;
}

// Compute this tick's target from immutable intent and actual match state.
Vector3 FormationTarget(const WorldState &world, const TacticalBoard &board,
                        const PlayerDirective *directive, const WorldPlayerState &player,
                        const TeamRequests &requests) {
  if (!directive || !directive->formation_position) return player.position;
  const int defend = world.teams[static_cast<unsigned>(player.side)].defending_direction;
  if (directive->role == PlannedPlayerRole::Goalkeeper)
    return Vector3(defend * (world.pitch.half_length() - 2.f),
                   std::clamp(world.ball_position.coords[1] * 0.2f, -3.f, 3.f), 0);
  Vector3 position = *directive->formation_position;
  position.coords[0] *= board.depth / 0.55f;
  position.coords[1] *= board.width / 0.75f;
  const auto marking = Requested(requests.pressure, world, player)
      ? requests.pressure.marking_target : directive->marking_target;
  if (marking) {
    const auto *opponent = Find(world, *marking);
    if (opponent && opponent->active && opponent->side != player.side)
      position = opponent->position + Vector3(defend * 2.f, 0, 0);
  }
  const float attack = static_cast<float>(-defend);
  position.coords[0] += world.ball_position.coords[0] * 0.2f +
                        attack * (TeamHasBall(world, player.side) ? 8.f : -3.f);
  position.coords[1] += world.ball_position.coords[1] * 0.15f;
  if (Requested(requests.attacking_run, world, player)) position.coords[0] += attack * 15.f;
  position.coords[0] = std::clamp(position.coords[0], -world.pitch.half_length() + 2.f,
                                 world.pitch.half_length() - 2.f);
  position.coords[1] = std::clamp(position.coords[1], -world.pitch.half_width() + 2.f,
                                 world.pitch.half_width() - 2.f);
  return position;
}
}  // namespace

bool DefaultAI::RequestAttackingRun(TeamSide side, const WorldState &world,
                                    std::optional<PlayerId> runner) {
  auto &intent = requests_.at(static_cast<unsigned>(side)).attacking_run;
  intent = {};
  if (!world.in_play || world.in_set_piece) return false;
  const WorldPlayerState *target = runner ? Find(world, *runner) : nullptr;
  if (!runner) {
    const auto *owner = BallOwner(world, side, boards_);
    if (!owner) return false;
    const int defend = world.teams[static_cast<unsigned>(side)].defending_direction;
    target = Closest(world, side, owner->position + Vector3(-defend * 26.f, 0, 0),
                     true, boards_, owner->id);
  }
  const auto *directive = target ? Directive(boards_, side, target->id) : nullptr;
  if (!target || !target->active || target->lazy || target->side != side ||
      (directive && directive->role == PlannedPlayerRole::Goalkeeper)) return false;
  intent = {target->id, std::nullopt, world.tick, 400, world.reset_sequence};
  return true;
}

bool DefaultAI::RequestTeamPressure(TeamSide side, const WorldState &world,
                                    std::optional<PlayerId> excluded) {
  auto &intent = requests_.at(static_cast<unsigned>(side)).pressure;
  intent = {};
  if (!world.in_play || world.in_set_piece) return false;
  const auto other = side == TeamSide::Home ? TeamSide::Away : TeamSide::Home;
  const auto *opponent = BallOwner(world, other, boards_);
  if (!opponent) return false;
  const int defend = world.teams[static_cast<unsigned>(side)].defending_direction;
  const auto *target = Closest(world, side, opponent->position + opponent->velocity * 0.24f +
                               Vector3(defend, 0, 0), true, boards_, excluded);
  if (!target) return false;
  intent = {target->id, opponent->id, world.tick, 50, world.reset_sequence};
  return true;
}

bool DefaultAI::RequestKeeperRush(TeamSide side, const WorldState &world) {
  auto &intent = requests_.at(static_cast<unsigned>(side)).keeper_rush;
  intent = {};
  if (!world.in_play || world.in_set_piece) return false;
  for (const auto &player : world.players) {
    const auto *directive = Directive(boards_, side, player.id);
    if (player.active && !player.lazy && player.side == side && directive &&
        directive->role == PlannedPlayerRole::Goalkeeper) {
      intent = {player.id, std::nullopt, world.tick, 30, world.reset_sequence};
      return true;
    }
  }
  return false;
}

void DefaultAI::Update(const WorldState &world, PlayerControlSet &output) const {
  const std::span<const TacticalBoard> boards = boards_;
  output.Clear();
  const Vector3 ball = world.ball_position.Get2D();
  const Vector3 intercept = ball + world.ball_velocity.Get2D() * 0.18f;
  for (const auto &player : world.players) {
    if (!player.active) continue;
    PlayerControl control;
    control.player = player.id;
    control.look_at = ball;
    const auto *directive = Directive(boards, player.side, player.id);
    const bool keeper = directive && directive->role == PlannedPlayerRole::Goalkeeper;
    const int defend = world.teams[static_cast<unsigned>(player.side)].defending_direction;
    const Vector3 goal(-defend * world.pitch.half_length(), 0, 0);
    const auto &requests = this->requests(player.side);
    Vector3 target = FormationTarget(world, tactics(player.side), directive, player, requests);

    if (!world.in_play || player.lazy ||
        (world.in_set_piece && world.restart_taker != player.id)) {
      control.move_direction = player.facing;
      output.Set(player.id, control);
      continue;
    }
    const auto *chaser = Closest(world, player.side, intercept, true, boards);
    if ((chaser && chaser->id == player.id) || Requested(requests.pressure, world, player)) target = intercept;
    if (keeper && (ball.coords[0] * defend > world.pitch.half_length() - 16.f ||
                   Requested(requests.keeper_rush, world, player))) target = intercept;

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
