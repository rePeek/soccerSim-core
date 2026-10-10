//
//  player_body_collision_shadow.hpp
//  football
//
//  Copyright 2026
//

#ifndef _HPP_PLAYER_BODY_COLLISION_SHADOW
#define _HPP_PLAYER_BODY_COLLISION_SHADOW

#include <array>
#include <cstdint>

#include "football/ball/ball_contact.hpp"
#include "model/player.hpp"
#include "sim/player/player_body_collider_motion.hpp"
#include "foundation/time/tick.hpp"

// Linear, side-effect-free first version of the body endpoint prediction. It
// deliberately does not call Humanoid::Process or PlayerLocomotion::Predict:
// an animation tick can mutate action state and consume RNG, while this P4b
// shadow must only observe the already-published kinematic state.
inline PlayerKinematicState PredictBodyKinematicState(
    const PlayerKinematicState& start, football::sim::TickSpan horizon) {
  PlayerKinematicState predicted = start;
  predicted.position += start.velocity * football::sim::ToSeconds(horizon);
  predicted.position.coords[2] = 0.0f;
  return predicted;
}

struct PlayerBodyMotionPrediction {
  football::model::PlayerId player = football::model::kInvalidPlayerId;
  PlayerKinematicState start;
  PlayerKinematicState end;
  std::array<football::ball::ColliderMotion, kPlayerBodyPartCount> colliders;
};

inline PlayerBodyMotionPrediction PredictBodyColliderMotions(
    football::model::PlayerId player, const PlayerKinematicState& start,
    football::sim::TickSpan horizon, const PlayerBodyColliderMotionIds& ids) {
  PlayerBodyMotionPrediction prediction;
  prediction.player = player;
  prediction.start = start;
  prediction.end = PredictBodyKinematicState(start, horizon);
  BuildBodyColliderMotions(prediction.start, prediction.end, ids,
                           prediction.colliders);
  return prediction;
}

// Read-only diagnostics accumulated by Simulation during P4b. The buckets are
// endpoint error in metres: [0,1cm), [1cm,5cm), [5cm,10cm), [10cm,25cm), >=25cm.
// They are neither snapshot state nor an input to rules, physics, RNG, or AI.
struct PlayerBodyCollisionShadowReport {
  std::uint64_t predicted_ticks = 0;
  std::uint64_t predicted_players = 0;
  std::uint64_t first_contacts = 0;
  std::uint64_t accepted_accidental_touches = 0;
  std::uint64_t matched_touches = 0;
  std::uint64_t missed_touches = 0;
  std::uint64_t false_positive_contacts = 0;
  std::uint64_t action_phase_conflicts = 0;
  std::array<std::uint64_t, 5> endpoint_error_bins{};
  float endpoint_error_sum = 0.0f;
  float endpoint_error_max = 0.0f;
};

inline std::size_t PlayerBodyEndpointErrorBin(float error_metres) {
  if (error_metres < 0.01f) return 0;
  if (error_metres < 0.05f) return 1;
  if (error_metres < 0.10f) return 2;
  if (error_metres < 0.25f) return 3;
  return 4;
}

#endif  // _HPP_PLAYER_BODY_COLLISION_SHADOW
