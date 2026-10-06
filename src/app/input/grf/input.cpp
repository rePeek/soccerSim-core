#include "app/input/grf/input.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace football::app::grf {
namespace {
using blunted::Vector3;
using model::PlayerId;
const std::array<Vector3, 8> directions{{
    Vector3(-1, 0, 0), Vector3(-1, 1, 0), Vector3(0, 1, 0), Vector3(1, 1, 0),
    Vector3(1, 0, 0), Vector3(1, -1, 0), Vector3(0, -1, 0), Vector3(-1, -1, 0)}};

bool Contains(std::span<const PlayerId> ids, PlayerId id) {
  return std::find(ids.begin(), ids.end(), id) != ids.end();
}
const WorldPlayerState *Find(const WorldState &world, PlayerId id) {
  for (const auto &player : world.players) if (player.id == id) return &player;
  return nullptr;
}
bool Keeper(const ai::TacticalBoard &board, PlayerId id) {
  for (const auto &player : board.players)
    if (player.player == id) return player.role == ai::PlannedPlayerRole::Goalkeeper;
  return false;
}
}  // namespace

std::vector<PlayerId> SelectInitialPlayers(const WorldState &world, model::TeamSide side,
                                          std::span<const PlayerId> eligible, std::size_t count) {
  std::vector<const WorldPlayerState *> candidates;
  for (const auto &player : world.players)
    if (player.active && player.side == side && Contains(eligible, player.id))
      candidates.push_back(&player);
  std::stable_sort(candidates.begin(), candidates.end(), [&](const auto *a, const auto *b) {
    return (a->position - world.ball_position.Get2D()).GetSquaredLength() <
           (b->position - world.ball_position.Get2D()).GetSquaredLength();
  });
  candidates.resize(std::min(count, candidates.size()));
  std::vector<PlayerId> selected;
  for (const auto &player : world.players)
    if (std::find(candidates.begin(), candidates.end(), &player) != candidates.end())
      selected.push_back(player.id);
  return selected;
}

Input::Input(const model::Team &team, model::TeamSide side) : side_(side) {
  const auto size = !team.formation.empty() ? team.formation.size()
      : !team.tactical_formation.empty() ? team.tactical_formation.size() : team.players.size();
  for (std::size_t i = 0; i < std::min(size, team.players.size()); ++i)
    if (team.formation.empty() || team.formation[i].controllable)
      eligible_.push_back(team.players[i].id);
}

bool Input::Eligible(const WorldPlayerState &player) const {
  return player.active && player.side == side_ && Contains(eligible_, player.id);
}
bool Input::Select(const WorldState &world, PlayerId id) {
  const auto *player = Find(world, id);
  if (!player || !Eligible(*player)) return false;
  selected_ = id;
  return true;
}
void Input::Reset() {
  selected_.reset();
  direction_ = Vector3(0);
  pending_action_.reset();
  switch_pending_ = disabled_ = sprint_ = dribble_ = pressure_ = team_pressure_ = keeper_rush_ = false;
}

bool Input::Apply(int wire_action) {
  if (wire_action < 0 || wire_action > static_cast<int>(Action::BuiltinAI)) return false;
  return Apply(static_cast<Action>(wire_action));
}
bool Input::Apply(Action action) {
  const int value = static_cast<int>(action);
  if (value < 0 || value > static_cast<int>(Action::BuiltinAI)) return false;
  disabled_ = action == Action::BuiltinAI;
  if (value >= 1 && value <= 8) { direction_ = directions[value - 1]; return true; }
  switch (action) {
    case Action::Idle: break;
    case Action::LongPass: pending_action_ = ControlAction::LongPass; break;
    case Action::HighPass: pending_action_ = ControlAction::HighPass; break;
    case Action::ShortPass: pending_action_ = ControlAction::ShortPass; break;
    case Action::Shot: pending_action_ = ControlAction::Shoot; break;
    case Action::Sliding: pending_action_ = ControlAction::Tackle; break;
    case Action::Switch: switch_pending_ = true; break;
    case Action::Sprint: sprint_ = true; break;
    case Action::Dribble: dribble_ = true; break;
    case Action::Pressure: pressure_ = true; break;
    case Action::TeamPressure: team_pressure_ = true; break;
    case Action::KeeperRush: keeper_rush_ = true; break;
    case Action::ReleaseDirection: direction_ = Vector3(0); break;
    case Action::ReleaseSprint: sprint_ = false; break;
    case Action::ReleaseDribble: dribble_ = false; break;
    case Action::ReleasePressure: pressure_ = false; break;
    case Action::ReleaseTeamPressure: team_pressure_ = false; break;
    case Action::ReleaseKeeperRush: keeper_rush_ = false; break;
    case Action::ReleaseSwitch: switch_pending_ = false; break;
    case Action::ReleaseLongPass:
      if (pending_action_ == ControlAction::LongPass) pending_action_.reset();
      break;
    case Action::ReleaseHighPass:
      if (pending_action_ == ControlAction::HighPass) pending_action_.reset();
      break;
    case Action::ReleaseShortPass:
      if (pending_action_ == ControlAction::ShortPass) pending_action_.reset();
      break;
    case Action::ReleaseShot:
      if (pending_action_ == ControlAction::Shoot) pending_action_.reset();
      break;
    case Action::ReleaseSliding:
      if (pending_action_ == ControlAction::Tackle) pending_action_.reset();
      break;
    case Action::BuiltinAI: pending_action_.reset(); switch_pending_ = false; break;
    default: break;  // Directions already handled above.
  }
  return true;
}

bool Input::IsStickyActionActive(Action action) const {
  const int value = static_cast<int>(action);
  if (value >= 1 && value <= 8) return direction_ == directions[value - 1];
  switch (action) {
    case Action::Sprint: return sprint_;
    case Action::Dribble: return dribble_;
    case Action::Pressure: return pressure_;
    case Action::TeamPressure: return team_pressure_;
    case Action::KeeperRush: return keeper_rush_;
    default: return false;
  }
}

void Input::Update(const WorldState &world, ai::DefaultAI &policy, PlayerControlSet &output,
                   std::span<const PlayerId> reserved) {
  output.Clear();
  const auto action = pending_action_;
  const bool switching = switch_pending_;
  pending_action_.reset();
  switch_pending_ = false;
  if (disabled_) return;

  const auto available = [&](const WorldPlayerState &player) {
    return Eligible(player) && !Contains(reserved, player.id);
  };
  const auto closest = [&](bool switching) {
    const WorldPlayerState *best = nullptr;
    float distance = std::numeric_limits<float>::max();
    for (const auto &player : world.players) {
      if (!available(player) || (switching && (selected_ == player.id ||
          Keeper(policy.tactics(side_), player.id)))) continue;
      const float candidate = (player.position - world.ball_position.Get2D()).GetSquaredLength();
      if (candidate < distance) { distance = candidate; best = &player; }
    }
    return best;
  };
  const auto *player = selected_ ? Find(world, *selected_) : nullptr;
  if (!player || !available(*player)) player = closest(false);
  // Actual restart authority is read, never overridden or cached in the input.
  if (world.in_set_piece && world.restart_taker) {
    const auto *taker = Find(world, *world.restart_taker);
    if (taker && available(*taker)) player = taker;
  }
  if (!player) { selected_.reset(); return; }
  selected_ = player->id;
  if (switching && world.in_play && !world.in_set_piece) {
    if (player->has_possession || world.ball_retainer == player->id) {
      policy.RequestAttackingRun(side_, world);
    } else if (const auto *next = closest(true)) {
      player = next;
      selected_ = next->id;
    }
  }
  if (team_pressure_) policy.RequestTeamPressure(side_, world, player->id);
  if (keeper_rush_) policy.RequestKeeperRush(side_, world);

  PlayerControl control;
  control.player = player->id;
  control.move_direction = direction_.GetNormalized(player->facing);
  control.look_at = world.ball_position.Get2D();
  const bool can_act = world.in_play && (!world.in_set_piece || world.restart_taker == player->id);
  if (can_act) {
    if (direction_.GetLength() > 0.f)
      control.desired_speed = std::min(player->max_speed, sprint_ ? 8.f : dribble_ ? 3.5f : 5.f);
    if (dribble_) control.action = ControlAction::Dribble;
    if (pressure_ && !player->has_possession) {
      control.move_direction = (world.ball_position.Get2D() + world.ball_velocity.Get2D() * 0.18f -
                                player->position).GetNormalized(player->facing);
      control.desired_speed = player->max_speed;
    }
    if (action) {
      control.action = *action;
      control.power = 0.6f;
      const int defend = world.teams[static_cast<unsigned>(side_)].defending_direction;
      const Vector3 aim = direction_.GetLength() > 0.f ? control.move_direction : Vector3(-defend, 0, 0);
      control.target_position = player->position + aim * 30.f;
      if (control.action == ControlAction::Shoot && direction_.GetLength() == 0.f)
        control.target_position = Vector3(-defend * world.pitch.half_length(), 0, 0);
    }
  }
  output.Set(player->id, control);
}
}  // namespace football::app::grf
