#ifndef FOOTBALL_ANIMATION_TRIP_MATCH_HPP
#define FOOTBALL_ANIMATION_TRIP_MATCH_HPP

#include <span>

#include "football/animation/clip.hpp"
#include "football/animation/entry_frame.hpp"
#include "football/animation/library.hpp"
#include "football/animation/types.hpp"

// ANIM-2 A14 — Trip clip + entry-frame matching.
//
// A Trip is chosen from the collision evidence: the fall direction against the
// clip's own bump/bodily direction, the incoming-velocity bucket against the
// actor's current speed, and how smoothly the actor's current pose can enter a
// legal frame of the clip. Pure: the Player supplies the values and installs the
// result; the physical impulse is never Animation's.
//
// ANIM-2 A14.0: the body lives in src/animation/trip_match.cpp.

namespace football::animation {

struct TripMatchRequest {
  // Contact direction in the actor's RUNTIME frame.
  Vector3 impact_direction;
  // Current committed velocity in the actor's runtime frame.
  Vector3 current_velocity;
  // The actor's current final pose, for clip-to-clip pose continuity.
  PoseFrame current_pose;
  bool has_current_pose = false;
  // The legacy trip type of the candidate set; -1 means "do not filter".
  int trip_type = -1;
  // Spatial angle, to rotate metadata (animation space) into comparison space.
  radian spatial_angle = 0.0f;
  // How many frames must remain after the entry frame.
  int min_remaining_frames = 1;
};

struct TripMatchResult {
  AnimationId clip_id = -1;
  int start_frame = 0;
  float score = 0.0f;  // lower is better
  bool matched = false;
};

// Rank the Trip candidates and pick the best (clip, entry frame). Candidates
// whose metadata is not a Trip are ignored. Ties break on the lower clip id.
// `matched` is true whenever at least one usable candidate was scored; the entry
// frame falls back to 0 when no legal frame exists.
TripMatchResult SelectTripMatch(const AnimationLibrary& library,
                                std::span<const int> candidates,
                                const TripMatchRequest& request);

}  // namespace football::animation

#endif  // FOOTBALL_ANIMATION_TRIP_MATCH_HPP
