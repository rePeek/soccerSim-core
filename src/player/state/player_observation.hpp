// Read-only observation interface for one Player call. Player owns this shape;
// the observation layer implements it over the captured match history. Player
// never stores the history, samples the raw capture or names MentalImage.
#ifndef FOOTBALL_PLAYER_OBSERVATION_HPP
#define FOOTBALL_PLAYER_OBSERVATION_HPP

#include <chrono>
#include <cstddef>
#include <vector>

#include "football/foundation/math/vector3.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/player/state/player_role.hpp"

namespace football::ball { class Ball; }

namespace football::sim::player {

// A value-only actor image for one capture. Unlike the observation-layer
// PlayerImage it holds no Player* or execution history.
struct PlayerImagePosition {
  PlayerImagePosition() = default;
  PlayerImagePosition(const football::math::Vector3& position,
                      const football::math::Vector3& movement,
                      e_PlayerRole player_role)
      : position(position), movement(movement), player_role(player_role) {}
  football::math::Vector3 position;
  football::math::Vector3 movement;
  e_PlayerRole player_role = e_PlayerRole_GK;
};

// Borrowed for a single tick; the adapter owns the capture history. The two age
// kinds mirror the legacy sampling (tick-grid vs continuous reaction delay) so
// the rounded capture slot is unchanged.
class PlayerObservation {
 public:
  virtual ~PlayerObservation() = default;
  virtual std::size_t Size() const = 0;
  virtual football::math::Vector3 BallPrediction(
      std::chrono::milliseconds age, int horizon_ms, Tick now,
      const football::ball::Ball& ball) const = 0;
  virtual football::math::Vector3 BallPrediction(
      std::chrono::milliseconds age, TickSpan horizon, Tick now,
      const football::ball::Ball& ball) const = 0;
  virtual std::vector<PlayerImagePosition> TeamPlayers(
      int team_id, std::chrono::milliseconds age, Tick now) const = 0;
};

}  // namespace football::sim::player

#endif  // FOOTBALL_PLAYER_OBSERVATION_HPP
