// ANIM-2 A14.0 — Trip clip + entry-frame matching.

#include "football/animation/trip_match.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace football::animation {

namespace {

// Direction dominates (the fall must follow the impact); velocity is a strong
// second; pose continuity breaks near-ties without overriding the collision.
constexpr float kDirectionWeight = 2.0f;
constexpr float kVelocityWeight = 1.0f;
constexpr float kPoseWeight = 0.5f;
// A Trip keeps at least half its frames after the entry, so the fall itself
// still plays instead of entering at the recovery phase.
constexpr float kTripMinRemainingFraction = 0.5f;

}  // namespace

TripMatchResult SelectTripMatch(const AnimationLibrary& library,
                                std::span<const int> candidates,
                                const TripMatchRequest& request) {
  TripMatchResult best;
  if (candidates.empty()) return best;

  const Vector3 rel_impact =
      request.impact_direction.GetRotated2D(-request.spatial_angle)
          .GetNormalized(Vector3(0, -1, 0));
  const float current_speed = request.current_velocity.GetLength();

  float best_score = std::numeric_limits<float>::max();
  for (const int id : candidates) {
    if (id < 0 || static_cast<std::size_t>(id) >= library.Size()) continue;
    const AnimationClip& clip = library.Get(static_cast<uint32_t>(id));
    if (clip.metadata.action_type != e_DefString_Trip) continue;
    if (request.trip_type >= 0 && clip.metadata.trip_type != request.trip_type)
      continue;

    // ANIM-2 A18.2: a Trip must keep enough of the loss-of-balance and fall to
    // play, so the entry frame is limited to the first part of the clip. Pose
    // similarity alone must not pick a late recovery frame.
    const int trip_min_remaining =
        static_cast<int>(static_cast<float>(clip.poses.size()) *
                          kTripMinRemainingFraction);
    EntryFrameResult entry;
    if (request.has_current_pose) {
      EntryFrameRequest entry_request;
      entry_request.reference_pose = &request.current_pose;
      entry_request.min_remaining_frames =
          std::max(request.min_remaining_frames, trip_min_remaining);
      entry = SelectEntryFrame(clip, entry_request);
    }

    // The fall direction: the clip's own bump direction, else its incoming body
    // direction. A clip with neither stays direction-neutral.
    const Vector3 fall =
        clip.metadata.bump_direction.GetNormalized(clip.metadata.incoming_body_direction);
    float direction_mismatch = 0.0f;
    if (fall.GetLength() > 0.001f) {
      direction_mismatch =
          1.0f - clamp(fall.GetDotProduct(rel_impact), -1.0f, 1.0f);
    }

    const float velocity_mismatch =
        std::fabs(RangeVelocity(clip.metadata.incoming_velocity) - current_speed) /
        sprintVelocity;
    const float pose_mismatch =
        entry.matched ? entry.pose_distance / static_cast<float>(kBodyPartCount) : 1.0f;

    const float score = kDirectionWeight * direction_mismatch +
                        kVelocityWeight * velocity_mismatch +
                        kPoseWeight * pose_mismatch;
    if (score < best_score || (score == best_score && id < best.clip_id)) {
      best_score = score;
      best.clip_id = static_cast<AnimationId>(id);
      best.start_frame = entry.matched ? entry.frame : 0;
      best.score = score;
      best.matched = true;
    }
  }
  return best;
}

}  // namespace football::animation
