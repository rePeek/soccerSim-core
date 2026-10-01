#include "control/control_system.hpp"

#include <stdexcept>

namespace {
void CheckTeam(TeamId team) {
  if (team >= ControlSystem::kTeamCount) {
    throw std::out_of_range("control team id is out of range");
  }
}
}  // namespace

void ControlSystem::SetCoach(TeamId team,
                             std::unique_ptr<CoachControl> control) {
  CheckTeam(team);
  coaches_[team] = std::move(control);
  team_plans_[team] = TeamPlan{.team = team};
}

void ControlSystem::SetPlayerControl(TeamId team, PlayerId player,
                                     std::unique_ptr<PlayerControl> control) {
  CheckTeam(team);
  for (PlayerSlot& slot : players_) {
    if (slot.player == player) {
      slot.team = team;
      slot.control = std::move(control);
      return;
    }
  }
  players_.push_back(PlayerSlot{team, player, std::move(control)});
}

void ControlSystem::Reset() {
  for (auto& coach : coaches_) {
    if (coach) coach->Reset();
  }
  for (auto& player : players_) {
    if (player.control) player.control->Reset();
  }
  player_intents_.clear();
  for (TeamId team = 0; team < kTeamCount; ++team) {
    team_plans_[team] = TeamPlan{.team = team};
  }
}

void ControlSystem::Step(const WorldStateView& world) {
  for (TeamId team = 0; team < kTeamCount; ++team) {
    TeamPlan plan{.team = team};
    if (coaches_[team]) {
      plan = coaches_[team]->Decide(CoachControlContext{team, world});
      plan.team = team;
    }
    team_plans_[team] = std::move(plan);
  }

  player_intents_.clear();
  player_intents_.reserve(players_.size());
  for (const PlayerSlot& slot : players_) {
    if (!slot.control) continue;
    player_intents_.push_back(ControlledPlayerIntent{
        slot.player,
        slot.control->Decide(
            PlayerControlContext{slot.player, world, team_plans_[slot.team]})});
  }
}

const TeamPlan& ControlSystem::team_plan(TeamId team) const {
  CheckTeam(team);
  return team_plans_[team];
}
