// Adapter from the observation-layer capture history to the Player-owned
// read-only observation interface. Lives in the observation module so Player
// never includes MentalImage.
#ifndef FOOTBALL_SIM_OBSERVATION_PLAYER_OBSERVATION_ADAPTER_HPP
#define FOOTBALL_SIM_OBSERVATION_PLAYER_OBSERVATION_ADAPTER_HPP

#include <chrono>
#include <cstddef>
#include <span>
#include <vector>

#include "player/player_observation.hpp"

class MentalImage;

namespace football::sim::observation {

class PlayerObservationAdapter final
    : public football::sim::player::PlayerObservation {
 public:
  explicit PlayerObservationAdapter(std::span<const MentalImage> history)
      : history_(history) {}

  std::size_t Size() const override { return history_.size(); }

  football::math::Vector3 BallPrediction(std::chrono::milliseconds age,
                                         int horizon_ms, Tick now,
                                         const football::ball::Ball& ball) const override;
  football::math::Vector3 BallPrediction(std::chrono::milliseconds age,
                                         TickSpan horizon, Tick now,
                                         const football::ball::Ball& ball) const override;

  std::vector<football::sim::player::PlayerImagePosition> TeamPlayers(
      int team_id, std::chrono::milliseconds age, Tick now) const override;

 private:
  const MentalImage& Sample(std::chrono::milliseconds age) const;
  std::span<const MentalImage> history_;
};

}  // namespace football::sim::observation

#endif  // FOOTBALL_SIM_OBSERVATION_PLAYER_OBSERVATION_ADAPTER_HPP
