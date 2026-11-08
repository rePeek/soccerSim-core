#ifndef FOOTBALL_SIM_RUNTIME_PLAYER_BALL_TABLE_HPP
#define FOOTBALL_SIM_RUNTIME_PLAYER_BALL_TABLE_HPP

#include <cstdint>
#include <span>
#include <vector>

#include "football/player/execution/player_ball_relation.hpp"

class Player;

namespace football::sim {

// Runtime-owned per-actor ball relations, aligned with each Team's ordered
// roster (bench included). This is the single owner of the reachability /
// possession facts that used to live on the Player; every consumer borrows
// them for one call through this table or through the spans it exposes.
//
// The staggering offset that decides when an actor's estimate is refreshed is
// Runtime scheduling data and lives beside the relation, not on the Player.
class PlayerBallTable {
 public:
  void Bind(std::span<Player* const> home_players,
            std::span<Player* const> away_players,
            std::vector<football::player::PlayerBallRelation>* home_relations,
            std::vector<std::uint8_t>* home_phases,
            std::vector<football::player::PlayerBallRelation>* away_relations,
            std::vector<std::uint8_t>* away_phases);
  void Unbind();

  // Mutable access for the refresh phase; writes are Runtime-only.
  football::player::PlayerBallRelation& Mutable(int team_id, const Player* player);
  std::span<football::player::PlayerBallRelation> MutableSlice(int team_id) const;
  std::span<std::uint8_t> PhaseSlice(int team_id) const;

  // Read-only access for consumers (rules, observation, contact adapters).
  const football::player::PlayerBallRelation& Read(int team_id,
                                                  const Player* player) const;
  std::span<const football::player::PlayerBallRelation> Slice(int team_id) const;

  bool bound() const { return home_players_.data() != nullptr; }

 private:
  int IndexOf(int team_id, const Player* player) const;

  std::span<Player* const> home_players_;
  std::span<Player* const> away_players_;
  std::vector<football::player::PlayerBallRelation>* home_relations_ = nullptr;
  std::vector<std::uint8_t>* home_phases_ = nullptr;
  std::vector<football::player::PlayerBallRelation>* away_relations_ = nullptr;
  std::vector<std::uint8_t>* away_phases_ = nullptr;
};

}  // namespace football::sim

#endif  // FOOTBALL_SIM_RUNTIME_PLAYER_BALL_TABLE_HPP
