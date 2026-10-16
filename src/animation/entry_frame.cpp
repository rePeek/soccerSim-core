// ANIM-2 A13.0 — the pose-based entry-frame match, moved into the runtime
// Animation archive. Pure value code: no Player/Ball/Simulation/RNG.

#include "football/animation/entry_frame.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace football::animation {

namespace {

// Position deltas are in metres and rotation deltas in radians. One metre of
// joint mismatch is weighted like ~1.5 rad of rotation, so a leg out of phase
// dominates over a small root-bob difference.
constexpr float kPositionWeight = 1.5f;

}  // namespace

float PoseDistance(const PoseFrame& a, const PoseFrame& b) {
  float distance = 0.0f;
  for (std::size_t j = 0; j < kBodyPartCount; ++j) {
    distance += static_cast<float>(
        a.orientations[j].GetRotationAngle(b.orientations[j]));
    distance += (a.positions[j] - b.positions[j]).GetLength() * kPositionWeight;
  }
  return distance;
}

EntryFrameResult SelectEntryFrame(const AnimationClip& clip,
                                  const EntryFrameRequest& request) {
  EntryFrameResult result;
  if (request.reference_pose == nullptr || clip.poses.empty()) return result;

  const int frame_count = static_cast<int>(clip.poses.size());
  const int min_remaining = request.min_remaining_frames > 1
                                ? request.min_remaining_frames
                                : 1;
  // Always intersect the caller's bound with the duration bound, so an explicit
  // last_legal_frame can never exceed the clip.
  int last = frame_count - 1 - min_remaining;
  if (request.last_legal_frame != kEntryFrameUnbounded)
    last = std::min(last, request.last_legal_frame);
  if (last < 0) return result;  // no legal entry frame

  float best_distance = std::numeric_limits<float>::max();
  int best_frame = 0;
  for (int frame = 0; frame <= last; ++frame) {
    const float distance =
        PoseDistance(*request.reference_pose, clip.poses[static_cast<std::size_t>(frame)]);
    if (distance < best_distance) {
      best_distance = distance;
      best_frame = frame;
    }
  }

  result.frame = best_frame;
  result.pose_distance = best_distance;
  result.matched = true;
  return result;
}

}  // namespace football::animation
