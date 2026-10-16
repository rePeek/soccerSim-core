#ifndef FOOTBALL_ANIMATION_TOUCH_SCORING_HPP
#define FOOTBALL_ANIMATION_TOUCH_SCORING_HPP

#include "football/animation/types.hpp"
#include "football/animation/clip.hpp"

// ANIM-1 A6.4 step 2a / ANIM-2 A12.3 - stateless touch/action-smuggle scoring.
//
// These are line-for-line relocations of pure arithmetic inside
// `Humanoid::GetBestCheatableAnimID`. They read no Player / Humanoid /
// Simulation state: everything they need travels in the request struct, so the
// Animation side owns the formula and the Player side owns the inputs.
//
// Behaviour is intended to be bit-identical: same clamping, same operation
// order, same floating-point expressions. The bodies live in
// src/animation/touch_scoring.cpp.

namespace football::animation {

// The tail of `GetBestCheatableAnimID` that turns the raw ball-contact smuggle
// vector into the value actually applied to the player. `raw_smuggle` is the
// world-space (already rotated) value the caller computed as
// `best_action_smuggle_vec2d.GetRotated2D(spatial_angle)`.
struct ResolveActionSmuggleRequest {
  Vector3 raw_smuggle{0};
  float touch_pos_z = 0.0f;
  int anim_touch_frame = 0;
  e_FunctionType function_type = e_FunctionType_None;
  bool ball_retained_by_player = false;
  radian predicted_angle = 0.0f;
  radian spatial_angle = 0.0f;
  Vector3 adapted_outgoing_movement{0};

  bool force_full_action_smuggle_discard = false;
  bool enable_action_smuggle_discard = false;
  float cheat_discard_distance = 0.0f;
  float cheat_discard_distance_multiplier = 0.0f;
  float max_smuggle_discard_distance = 0.0f;
  bool discard_forward_smuggle = false;
  bool discard_sideways_smuggle = false;

  float closest_opponent_distance = 1.0f;
};

Vector3 ResolveActionSmuggle(const ResolveActionSmuggleRequest& request);

// ANIM-1 A6.4 step 9c: the pure body/ball distance-advantage scoring relocated
// from `Humanoid::GetBodyBallDistanceAdvantage`. Only `frame_count` is needed
// from the clip; every other fact travels in the arguments. The two cheat tuning
// values are passed explicitly so Animation owns no gameplay tuning.
float EvaluateBodyBallDistanceAdvantage(
    int frame_count, const Vector3 &animTouchMovement,
    const Vector3 &touchMovement, const Vector3 &incomingMovement,
    const Vector3 &outgoingMovement, radian outgoingAngle,
    const Vector3 &bodyPos, const Vector3 &FFO, const Vector3 &animBallPos2D,
    const Vector3 &actualBallPos2D, float radiusFactor,
    float radiusCheatDistance, float cheat_factor,
    float cheat_distance_bonus);

}  // namespace football::animation

#endif  // FOOTBALL_ANIMATION_TOUCH_SCORING_HPP
