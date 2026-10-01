#ifndef FOOTBALL_CONTROL_CONTROL_SYSTEM_HPP
#define FOOTBALL_CONTROL_CONTROL_SYSTEM_HPP

#include <array>
#include <memory>
#include <span>
#include <vector>

#include "control/coach/coach_control.hpp"
#include "control/player/player_control.hpp"

struct ControlledPlayerIntent {
  PlayerId player = kInvalidPlayerId;
  PlayerIntent intent;
};

// Coordinates decision-domain controls. It has no simulation authority: it
// reads a WorldStateView and publishes value intents for simulation to execute.
class ControlSystem {
 public:
  static constexpr TeamId kTeamCount = 2;

  void SetCoach(TeamId team, std::unique_ptr<CoachControl> control);
  void SetPlayerControl(TeamId team, PlayerId player,
                        std::unique_ptr<PlayerControl> control);
  void Reset();
  void Step(const WorldStateView& world);

  const TeamPlan& team_plan(TeamId team) const;
  std::span<const ControlledPlayerIntent> player_intents() const {
    return player_intents_;
  }

 private:
  struct PlayerSlot {
    TeamId team = kInvalidTeamId;
    PlayerId player = kInvalidPlayerId;
    std::unique_ptr<PlayerControl> control;
  };

  std::array<std::unique_ptr<CoachControl>, kTeamCount> coaches_;
  std::array<TeamPlan, kTeamCount> team_plans_;
  std::vector<PlayerSlot> players_;
  std::vector<ControlledPlayerIntent> player_intents_;
};

#endif  // FOOTBALL_CONTROL_CONTROL_SYSTEM_HPP
