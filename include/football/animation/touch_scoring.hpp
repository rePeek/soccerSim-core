#ifndef FOOTBALL_ANIMATION_TOUCH_SCORING_HPP
#define FOOTBALL_ANIMATION_TOUCH_SCORING_HPP

#include <algorithm>
#include <cmath>

#include "football/animation/types.hpp"
#include "football/animation/clip.hpp"

// ANIM-1 A6.4 step 2a - stateless touch/action-smuggle scoring helpers.
//
// These are line-for-line relocations of pure arithmetic inside
// `Humanoid::GetBestCheatableAnimID`. They read no Player / Humanoid /
// Simulation state: everything they need travels in the request struct, so the
// Animation side owns the formula and the Player side owns the inputs.
//
// Behaviour is intended to be bit-identical: same clamping, same operation
// order, same floating-point expressions.

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

inline Vector3 ResolveActionSmuggle(const ResolveActionSmuggleRequest& request) {
  Vector3 action_smuggle = request.raw_smuggle;

  if (request.force_full_action_smuggle_discard) {
    action_smuggle = 0;
  } else if (request.enable_action_smuggle_discard) {
    // cheat discard distance (don't show some amount of cheat)
    float smuggleDistance = action_smuggle.GetLength();
    float adaptedCheatDiscardDistanceMultiplier = request.cheat_discard_distance_multiplier;
    if (request.function_type != e_FunctionType_BallControl) adaptedCheatDiscardDistanceMultiplier *= 0.8f;
    if (request.touch_pos_z > 0.5f) adaptedCheatDiscardDistanceMultiplier *= 0.7f;
    if (request.touch_pos_z > 1.0f) adaptedCheatDiscardDistanceMultiplier *= 0.7f;
    float cheatDiscardDistanceBonus = 0.0f;
    if (request.function_type == e_FunctionType_Interfere) cheatDiscardDistanceBonus += 0.1f;  // don't break flow

    // smuggle, in this context, is how much we will move to the ball after the
    // physics have been accounted for.
    smuggleDistance = clamp(smuggleDistance - (request.cheat_discard_distance + cheatDiscardDistanceBonus), 0.0f, 100.0);
    smuggleDistance *= 1.0f - adaptedCheatDiscardDistanceMultiplier;
    smuggleDistance = std::min(smuggleDistance, action_smuggle.GetLength() - request.max_smuggle_discard_distance);

    if (request.ball_retained_by_player) smuggleDistance = 0.0f;
    action_smuggle = action_smuggle.GetNormalized(0) * smuggleDistance;

    // lose forward-facing part of smuggle
    if (request.discard_forward_smuggle || request.discard_sideways_smuggle) {
      radian toStraightAngle = request.spatial_angle + request.predicted_angle;
      action_smuggle.Rotate2D(-toStraightAngle);

      if (request.discard_forward_smuggle) {
        float shortenForwardDistance = 0.02f;
        float allowForwardDistance =
            0.25f *
            (1.0f -
             std::pow(NormalizedClamp(request.adapted_outgoing_movement.GetLength(),
                                      idleVelocity, sprintVelocity - 2.0f),
                      0.6f)) *
            (request.anim_touch_frame * 0.1f);
        if (request.function_type != e_FunctionType_BallControl && request.function_type != e_FunctionType_Trap) allowForwardDistance += 0.1f;
        if (action_smuggle.coords[1] < 0.0f) action_smuggle.coords[1] = clamp(action_smuggle.coords[1] + shortenForwardDistance, -allowForwardDistance, 0.0f);
      }
      if (request.discard_sideways_smuggle) {
        action_smuggle.coords[0] *= 0.7f;
      }

      action_smuggle.Rotate2D(toStraightAngle);
    }

    // less chaos in micro battles
    action_smuggle *= 0.7f + 0.3f * NormalizedClamp(request.closest_opponent_distance, 0.6f, 1.2f);
  }

  return action_smuggle;
}

}  // namespace football::animation

#endif  // FOOTBALL_ANIMATION_TOUCH_SCORING_HPP
