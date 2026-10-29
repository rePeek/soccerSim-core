#include "runtime/player_ball_table.hpp"

#include <cassert>
#include <stdexcept>

#include "player/player.hpp"

namespace football::sim {

void PlayerBallTable::Bind(
    std::span<Player* const> home_players, std::span<Player* const> away_players,
    std::vector<football::player::PlayerBallRelation>* home_relations,
    std::vector<std::uint8_t>* home_phases,
    std::vector<football::player::PlayerBallRelation>* away_relations,
    std::vector<std::uint8_t>* away_phases) {
  home_players_ = home_players;
  away_players_ = away_players;
  home_relations_ = home_relations;
  home_phases_ = home_phases;
  away_relations_ = away_relations;
  away_phases_ = away_phases;
}

void PlayerBallTable::Unbind() {
  home_players_ = {};
  away_players_ = {};
  home_relations_ = nullptr;
  home_phases_ = nullptr;
  away_relations_ = nullptr;
  away_phases_ = nullptr;
}

int PlayerBallTable::IndexOf(int team_id, const Player* player) const {
  const std::span<Player* const> roster =
      team_id == 0 ? home_players_ : away_players_;
  for (std::size_t i = 0; i < roster.size(); ++i) {
    if (roster[i] == player) return static_cast<int>(i);
  }
  return -1;
}

football::player::PlayerBallRelation& PlayerBallTable::Mutable(int team_id,
                                                              const Player* player) {
  const int index = IndexOf(team_id, player);
  assert(index >= 0 && "ball relation lookup requires a bound roster");
  std::vector<football::player::PlayerBallRelation>& relations =
      team_id == 0 ? *home_relations_ : *away_relations_;
  if (index < 0 || static_cast<std::size_t>(index) >= relations.size())
    throw std::out_of_range("ball relation lookup outside the roster");
  return relations[static_cast<std::size_t>(index)];
}

const football::player::PlayerBallRelation& PlayerBallTable::Read(
    int team_id, const Player* player) const {
  const int index = IndexOf(team_id, player);
  assert(index >= 0 && "ball relation lookup requires a bound roster");
  static const football::player::PlayerBallRelation kUnknown;
  const std::vector<football::player::PlayerBallRelation>& relations =
      team_id == 0 ? *home_relations_ : *away_relations_;
  if (index < 0 || static_cast<std::size_t>(index) >= relations.size())
    return kUnknown;
  return relations[static_cast<std::size_t>(index)];
}

std::span<football::player::PlayerBallRelation> PlayerBallTable::MutableSlice(
    int team_id) const {
  std::vector<football::player::PlayerBallRelation>& relations =
      team_id == 0 ? *home_relations_ : *away_relations_;
  return relations;
}

std::span<std::uint8_t> PlayerBallTable::PhaseSlice(int team_id) const {
  std::vector<std::uint8_t>& phases = team_id == 0 ? *home_phases_ : *away_phases_;
  return phases;
}

std::span<const football::player::PlayerBallRelation> PlayerBallTable::Slice(
    int team_id) const {
  const std::vector<football::player::PlayerBallRelation>& relations =
      team_id == 0 ? *home_relations_ : *away_relations_;
  return relations;
}

}  // namespace football::sim
