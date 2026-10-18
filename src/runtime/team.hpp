#pragma once

#include "football/runtime/team.hpp"
#include "runtime/roster.hpp"

namespace football::sim {
// Match entity / ownership boundary, never a simulation or AI controller.
// Its address and Player heap addresses remain stable for this match.
class Team final {
 public:
  explicit Team(TeamConfig config);
  ~Team();
  Team(const Team&) = delete;
  Team& operator=(const Team&) = delete;
  Team(Team&&) = delete;
  Team& operator=(Team&&) = delete;

  std::span<Player* const> Players() const { return roster_.Players(); }
  Player* Goalkeeper() const { return roster_.Goalie(); }
  Player* FindPlayer(football::player::PlayerId id) const;
  const std::string& Name() const { return config_.name; }
  const TeamLineup& InitialLineup() const { return config_.initial_lineup; }
  const TeamLeadership& Leadership() const { return config_.leadership; }
  const SetPieceOrders& SetPieceOrdersConfig() const { return config_.set_piece_orders; }
  const FormationPresetLibrary& FormationPresets() const { return config_.formation_presets; }
  const InitialFormation& InitialFormationConfig() const { return config_.initial_formation; }
  const TacticalFormation& TacticalFormationConfig() const { return config_.tactical_formation; }
  const std::map<std::string,float>& InitialTactics() const { return config_.legacy_tactics; }

  // Runtime-only bridge for existing explicit roster algorithms / construction.
  Roster& RosterStorage() { return roster_; }
  const Roster& RosterStorage() const { return roster_; }

 private:
  const TeamConfig config_;  // no PlayerDefinition array or current TacticalBoard
  Roster roster_;           // unique actor owner, stable declaration order
};
}  // namespace football::sim
