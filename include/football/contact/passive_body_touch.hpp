#ifndef FOOTBALL_CONTACT_PASSIVE_BODY_TOUCH_HPP
#define FOOTBALL_CONTACT_PASSIVE_BODY_TOUCH_HPP

#include <optional>
#include "football/foundation/math/vector3.hpp"
#include "football/player/state/player_action.hpp"

namespace football::contact {

// Accumulated in caller-supplied player/volume order. Runtime retains the
// notification between motion accumulation and bias sampling.
struct PassiveBodyTouchAccumulator {
  football::math::Vector3 bounce;
  float bias = 0.0f;
  int count = 0;
};

struct PassiveBodyTouchResult {
  // Historical endpoint velocity response, not a physical impulse J.
  std::optional<football::math::Vector3> velocity;
  float rotation_bias = 0.0f;
};

// Empirical action/possession reach used by the passive body-touch model.
// This is an effective detection radius, not BallModel's physical radius.
float PassiveBodyTouchRadius(e_FunctionType action, bool has_possession);

void AccumulatePassiveBodyMotion(PassiveBodyTouchAccumulator& accumulator,
    const football::math::Vector3& ball_position,
    const football::math::Vector3& volume_center,
    const football::math::Vector3& player_velocity, float opponent_touch_bias);
void AccumulatePassiveBodyBias(PassiveBodyTouchAccumulator& accumulator,
    const football::math::Vector3& ball_position,
    const football::math::Vector3& volume_center,
    float ball_radius, float volume_radius);
PassiveBodyTouchResult FinishPassiveBodyTouches(
    PassiveBodyTouchAccumulator accumulator,
    const football::math::Vector3& ball_velocity);

}  // namespace football::contact
#endif
