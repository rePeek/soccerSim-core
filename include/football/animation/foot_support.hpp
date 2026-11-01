#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "football/animation/clip.hpp"
#include "football/animation/types.hpp"
#include "football/foundation/math/vector3.hpp"

// Foot support phases, as an animation-domain fact.
//
// A gait clip knows which foot is carrying weight and when. Player Execution
// needs that answer to decide when a world-space support anchor may be planted
// or released; deriving it from ankle height alone mislabels authored clips (a
// swinging foot can hang lower than a stance foot), and deriving it from the
// settled pose cannot work at all (a locked foot keeps its height).
//
// The phases are derived from the clip's own pose track at load time
// (`DeriveFootSupport`), not stored in the artifact: one implementation, no
// Python/C++ duplication, and a bake-time track can replace it later without
// changing a single caller.

#include "football/animation/skeleton.hpp"

// Metres above the clip's lowest ankle at which a foot still counts as down,
// and the level at which it is considered lifted again (hysteresis).
inline constexpr float kFootDownHeight = 0.04f;
inline constexpr float kFootLiftHeight = 0.09f;

// Derives the per-frame support track from the clip's own leg poses.
//
// `clip.foot_support` and `clip.stance_speed` are filled by AnimationLibrary on
// load. `stance_speed` is how fast a stance foot travels backwards inside the
// clip, in metres per second: it is the speed the *foot* implies, which for
// authored clips differs from the root track (the animation has foot slide baked
// in). Playback prefers it, so the plant does not have to absorb the difference.
void DeriveFootSupport(AnimationClip& clip);

// One support frame at a fractional clip time. Nearest baked frame, clamped at
// both ends; a clip without a derived track reports both feet swinging, which
// tells the caller to fall back to its own detection.
FootSupportFrame SampleFootSupport(const AnimationClip& clip, float frame_time);

// RMA-1: measures whether the clip's own root track and support foot agree
// enough for the IK-free adaptation. Runs after DeriveFootSupport and fills
// `clip.motion_quality`; it never modifies the clip's motion or poses. A clip
// that fails the gate keeps the compatibility path - the flags are a
// diagnostic of the source animation, not a clamp.
void DeriveMotionQuality(AnimationClip& clip);
