#include "observation/player_observation_adapter.hpp"

#include <vector>

#include "football/ball/ball.hpp"
#include "observation/mentalimage.hpp"
#include "observation/mentalimage_sampling.hpp"

namespace football::sim::observation {

using football::sim::player::PlayerImagePosition;

const MentalImage& PlayerObservationAdapter::Sample(
    std::chrono::milliseconds age) const {
  return history_[MentalImageSampleIndex(history_.size(), age)];
}

football::math::Vector3 PlayerObservationAdapter::BallPrediction(
    std::chrono::milliseconds age, int horizon_ms, Tick now,
    const football::ball::Ball& ball) const {
  return Sample(age).GetBallPrediction(horizon_ms, now, ball);
}

football::math::Vector3 PlayerObservationAdapter::BallPrediction(
    std::chrono::milliseconds age, TickSpan horizon, Tick now,
    const football::ball::Ball& ball) const {
  return Sample(age).GetBallPrediction(horizon, now, ball);
}

std::vector<PlayerImagePosition> PlayerObservationAdapter::TeamPlayers(
    int team_id, std::chrono::milliseconds age, Tick now) const {
  std::vector<PlayerImagePosition> result;
  const auto images = Sample(age).GetTeamPlayerImages(team_id, now);
  result.reserve(images.size());
  for (const auto& image : images) {
    result.emplace_back(image.position, image.movement, image.player_role);
  }
  return result;
}

}  // namespace football::sim::observation
