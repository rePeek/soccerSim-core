// Copyright 2026
#ifndef _HPP_PLAYER_BODY_COLLISION_SHADOW
#define _HPP_PLAYER_BODY_COLLISION_SHADOW

#include <array>
#include <cstdint>
#include <optional>

#include "football/ball/ball_contact.hpp"
#include "model/player.hpp"
#include "sim/player/player_action.hpp"
#include "sim/player/player_body_collider_motion.hpp"
#include "foundation/time/tick.hpp"

// Read-only linear prediction. Never executes animation/locomotion or draws RNG.
inline PlayerKinematicState PredictBodyKinematicState(
    const PlayerKinematicState& start, football::sim::TickSpan horizon) {
  PlayerKinematicState predicted = start;
  predicted.position += start.velocity * football::sim::ToSeconds(horizon);
  return predicted;
}

struct PlayerBodyMotionPrediction {
  football::model::PlayerId player = football::model::kInvalidPlayerId;
  PlayerKinematicState start;
  PlayerKinematicState end;
  std::array<football::ball::ColliderMotion, kPlayerBodyPartCount> colliders;
  e_FunctionType action = e_FunctionType_None;
  std::optional<float> endpoint_error;
  bool turned = false;
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

// Metres: [0,1cm), [1cm,5cm), [5cm,10cm), [10cm,25cm), >=25cm.
struct BodyEndpointErrors {
  std::uint64_t samples = 0;
  std::array<std::uint64_t, 5> bins{};
  double sum = 0;
  float maximum = 0;

  void Record(float error) {
    ++samples;
    ++bins[error < .01f ? 0 : error < .05f ? 1 : error < .10f ? 2 : error < .25f ? 3 : 4];
    sum += error;
    if (error > maximum) maximum = error;
  }
};

// No physics/rules consumer reads this report. Categories: movement, sliding,
// trip/fall, other animation actions. Turning (>0.05 radians/tick) also measured.
struct PlayerBodyCollisionShadowReport {
  std::uint64_t predicted_ticks = 0;
  std::uint64_t discarded_ticks = 0;
  std::uint64_t predicted_players = 0;
  std::uint64_t first_contacts = 0;
  std::uint64_t accepted_accidental_touches = 0;
  std::uint64_t matched_touches = 0;
  std::uint64_t missed_touches = 0;
  std::uint64_t false_positive_contacts = 0;
  std::uint64_t action_phase_conflicts = 0;
  BodyEndpointErrors endpoint_errors;
  std::array<BodyEndpointErrors, 4> action_errors;
  BodyEndpointErrors turning_errors;
};

inline std::size_t BodyShadowActionCategory(e_FunctionType action) {
  if (action == e_FunctionType_Movement) return 0;
  if (action == e_FunctionType_Sliding) return 1;
  if (action == e_FunctionType_Trip) return 2;
  return 3;
}

#endif
