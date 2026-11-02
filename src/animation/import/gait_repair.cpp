#include "import.hpp"
#include "football/animation/foot_support.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace animation_import {
// RM-1: import-time gait repair.
//
// A shipped half-cycle (one step) is not a loop: its first and last instants
// are mirrored, not identical. The catalog already pairs it with its mirror,
// so the two halves form the full cycle. Concatenating them fixes the pose
// seam; deriving the root from the clip's own stance foot removes the authored
// foot slide; and retiming the cycle by stance-foot travel makes the root a
// constant-velocity track, which is what makes the result playable *and*
// loopable without Foot IK.
//
// This is a repair of the value the importer hands to the runtime, not a new
// disk format and not a runtime correction. A candidate is installed only when
// the repaired clip itself passes the strict motion-quality gate; otherwise the
// original GLB track is left untouched and keeps the compatibility fallback.

Vector3 GaitAnkle(const PoseFrame& pose, BodyPart thigh, BodyPart knee,
                  BodyPart ankle) {
  const Quaternion body_rotation = pose.orientations[BodyPart::body];
  const Vector3 body_origin = PlayerBindOffset(BodyPart::body);
  const Vector3 hip =
      body_origin + body_rotation * PlayerBindOffset(thigh);
  const Quaternion thigh_rotation =
      (body_rotation * pose.orientations[thigh]).GetNormalized();
  const Vector3 knee_position = hip + thigh_rotation * PlayerBindOffset(knee);
  const Quaternion knee_rotation =
      (thigh_rotation * pose.orientations[knee]).GetNormalized();
  return knee_position + knee_rotation * PlayerBindOffset(ankle);
}

float PoseAngle(const PoseFrame& a, const PoseFrame& b) {
  float worst = 0.0f;
  for (std::size_t joint = 0; joint < kBodyPartCount; ++joint) {
    const float dot = std::clamp(
        std::fabs(a.orientations[joint].GetDotProduct(b.orientations[joint])),
        0.0f, 1.0f);
    worst = std::max(worst, 2.0f * std::acos(dot));
  }
  return worst;
}

bool StraightGait(const AnimationClip& clip) {
  if (clip.root_positions.size() < 2) return false;
  const Vector3 travel =
      (clip.root_positions.back() - clip.root_positions.front()).Get2D();
  if (travel.GetLength() <= 1e-3f) return false;
  const Vector3 direction = travel.GetNormalized();
  for (std::size_t f = 0; f + 1 < clip.root_positions.size(); ++f) {
    const Vector3 step =
        (clip.root_positions[f + 1] - clip.root_positions[f]).Get2D();
    if (step.GetLength() <= 1e-4f) continue;
    if (step.GetNormalized().GetDotProduct(direction) < 0.985f) return false;
  }
  return true;
}

PoseFrame InterpolateGaitPose(const PoseFrame& a, const PoseFrame& b,
                             float t) {
  PoseFrame pose;
  for (std::size_t joint = 0; joint < kBodyPartCount; ++joint) {
    pose.orientations[joint] =
        a.orientations[joint].GetSlerped(t, b.orientations[joint]);
    pose.positions[joint] =
        a.positions[joint] * (1.0f - t) + b.positions[joint] * t;
  }
  return pose;
}

// One full gait cycle from a half-cycle and its mirror. The mirror's first
// sample is the same gait instant as the half-cycle's last sample, so it is
// the seam, not a new frame.
AnimationClip MakeGaitCycle(const AnimationClip& first,
                            const AnimationClip& second) {
  Check(first.poses.size() == second.poses.size(),
        "gait pair has different frame counts");
  const std::size_t count = first.poses.size();
  AnimationClip cycle = first;
  cycle.poses.resize(2 * count - 1);
  cycle.root_positions.resize(2 * count - 1);
  const Vector3 seam = first.root_positions[count - 1];
  for (std::size_t frame = 1; frame < count; ++frame) {
    cycle.poses[count - 1 + frame] = second.poses[frame];
    cycle.root_positions[count - 1 + frame] =
        seam + (second.root_positions[frame] - second.root_positions[0]);
  }
  cycle.frame_count = static_cast<uint32_t>(cycle.poses.size());
  cycle.has_contact = false;
  cycle.contact_position = Vector3(0);
  cycle.touches.clear();
  return cycle;
}

// Resample the cycle at a constant stance-foot travel per sample. The pose
// timing follows the foot; the root becomes a straight, constant-speed track.
// That is the difference between "the foot happens to be planted" and a gait
// the Player can play at any bounded rate without a root-speed pop.
AnimationClip RetimeGaitCycle(const AnimationClip& source) {
  const std::size_t count = source.poses.size();
  if (count < 3 || source.foot_support.size() != count) return source;
  std::vector<Vector3> left;
  std::vector<Vector3> right;
  left.reserve(count);
  right.reserve(count);
  for (std::size_t frame = 0; frame < count; ++frame) {
    left.push_back(GaitAnkle(source.poses[frame], BodyPart::left_thigh,
                             BodyPart::left_knee, BodyPart::left_ankle));
    right.push_back(GaitAnkle(source.poses[frame], BodyPart::right_thigh,
                              BodyPart::right_knee, BodyPart::right_ankle));
  }
  const Vector3 travel =
      (source.root_positions.back() - source.root_positions.front()).Get2D();
  if (travel.GetLength() <= 1e-3f) return source;
  const Vector3 heading = travel.GetNormalized();
  const Vector3 origin = source.root_positions.front();
  std::vector<float> travel_at(count, 0.0f);
  float total = 0.0f;
  for (std::size_t frame = 0; frame + 1 < count; ++frame) {
    const bool left_support =
        FootIsSupporting(source.foot_support[frame].left) &&
        FootIsSupporting(source.foot_support[frame + 1].left);
    const bool right_support =
        FootIsSupporting(source.foot_support[frame].right) &&
        FootIsSupporting(source.foot_support[frame + 1].right);
    const auto backward = [&](const Vector3& before, const Vector3& after) {
      return std::max(0.0f, (before - after).Get2D().GetLength());
    };
    // The retime is driven by the stance foot only. A flight interval has no
    // planted reference to time the cycle against, so it contributes no ground
    // travel; the *output* root is still a constant-speed track. Using the
    // authored root here would re-introduce the authored foot slide as timing.
    float step = 0.0f;
    if (left_support && right_support) {
      step = 0.5f * (backward(left[frame], left[frame + 1]) +
                     backward(right[frame], right[frame + 1]));
    } else if (left_support) {
      step = backward(left[frame], left[frame + 1]);
    } else if (right_support) {
      step = backward(right[frame], right[frame + 1]);
    }
    total += step;
    travel_at[frame + 1] = total;
  }
  if (total <= 1e-3f) return source;
  AnimationClip result = source;
  for (std::size_t frame = 0; frame < count; ++frame) {
    const float distance =
        total * static_cast<float>(frame) / static_cast<float>(count - 1);
    auto upper = std::lower_bound(travel_at.begin(), travel_at.end(), distance);
    std::size_t hi = static_cast<std::size_t>(upper - travel_at.begin());
    if (hi == 0) hi = 1;
    if (hi >= count) hi = count - 1;
    const std::size_t lo = hi - 1;
    const float span = travel_at[hi] - travel_at[lo];
    const float fraction =
        span > 1e-9f ? (distance - travel_at[lo]) / span : 0.0f;
    result.poses[frame] = InterpolateGaitPose(
        source.poses[lo], source.poses[hi], std::clamp(fraction, 0.0f, 1.0f));
    result.root_positions[frame] = origin + heading * distance;
    result.poses[frame].positions[BodyPart::player].coords[0] =
        result.root_positions[frame].coords[0];
    result.poses[frame].positions[BodyPart::player].coords[1] =
        result.root_positions[frame].coords[1];
  }
  result.frame_count = static_cast<uint32_t>(count);
  return result;
}


// RM-F4: a repaired cycle changes length and native root speed. Selection
// metadata that still claims the old speed would ask the adapter to play a
// clip outside its bounded range or label a walk as a sprint. Clamp the
// declared in/out motion to what the repaired root can actually reach; this
// keeps the existing velocity bucket truthful without inventing a new field.
// Mirrors the Player's kLocomotionMaxPlaybackRate; the Animation target
// cannot include a Player header. Keep the two values in step.
constexpr float kGaitRepairMaxPlaybackRate = 2.2f;

void RevalidateRepairedMetadata(AnimationClip& clip) {
  const float reachable =
      clip.motion_quality.root_speed * kGaitRepairMaxPlaybackRate;
  const auto clamp_speed = [&](float& speed) {
    if (speed > reachable) speed = reachable;
  };
  clamp_speed(clip.metadata.incoming_velocity);
  clamp_speed(clip.metadata.outgoing_velocity);
  const auto clamp_movement = [&](football::math::Vector3& movement) {
    const float length = movement.GetLength();
    if (length > reachable && length > 1e-6f) {
      movement = movement * (reachable / length);
    }
  };
  clamp_movement(clip.metadata.incoming_movement);
  clamp_movement(clip.metadata.outgoing_movement);
  clip.metadata.translation =
      clip.root_positions.back() - clip.root_positions.front();
}

void RepairGaitCycles(std::vector<AnimationClip>& clips,
                      const std::vector<int>& partner) {
  for (std::size_t index = 0; index < clips.size(); ++index) {
    const int other = partner[index];
    if (other < 0 || static_cast<std::size_t>(other) < index) continue;
    const AnimationClip& first = clips[index];
    const AnimationClip& second = clips[static_cast<std::size_t>(other)];
    const bool debug = std::getenv("FOOTBALL_GAIT_DEBUG") != nullptr;
    const auto reject = [&](const char* why) {
      if (debug) {
        std::cerr << "AnimationLibrary: gait candidate " << first.name
                  << " rejected: " << why << '\n';
      }
    };
    if (first.metadata.action_type != e_DefString_Movement ||
        second.metadata.action_type != e_DefString_Movement) {
      reject("not a movement clip");
      continue;
    }
    if (first.poses.size() < 5 || first.poses.size() != second.poses.size() ||
        first.root_positions.size() != first.poses.size() ||
        second.root_positions.size() != second.poses.size()) {
      reject("frame counts do not pair");
      continue;
    }
    if (!StraightGait(first) || !StraightGait(second)) {
      reject("root track is not straight");
      continue;
    }
    AnimationClip probe = first;
    DeriveMotionQuality(probe);
    if (!probe.motion_quality.tracked ||
        probe.motion_quality.root_speed <= 0.5f ||
        probe.motion_quality.stance_direction_alignment < 0.9f) {
      reject("no straight stance reference");
      continue;
    }
    if (!FootSupportTrusted(probe.foot_support_summary,
                            !probe.foot_support.empty())) {
      reject("support track is untrusted");
      continue;
    }
    // The second half is entered at its second sample: its first sample is the
    // same gait instant as the half-cycle's last sample. Check the real seam.
    if (PoseAngle(first.poses.back(), second.poses[1]) > 0.35f) {
      reject("half-cycle pose seam");
      continue;
    }

    AnimationClip cycle = MakeGaitCycle(first, second);
    DeriveFootSupport(cycle);
    cycle = RetimeGaitCycle(cycle);
    DeriveFootSupport(cycle);
    DeriveMotionQuality(cycle);
    if (!cycle.motion_quality.ik_free_usable) {
      if (debug) {
        const auto& q = cycle.motion_quality;
        std::cerr << "  cycle root=" << q.root_speed
                  << " stance=" << q.stance_speed
                  << " slip=" << q.support_slip_speed
                  << " worst=" << q.worst_support_slip_speed
                  << " align=" << q.stance_direction_alignment
                  << " seamR=" << q.seam_root_speed_delta
                  << " seamP=" << q.seam_pose_step
                  << " cal=" << q.stance_calibrated
                  << " trusted="
                  << FootSupportTrusted(cycle.foot_support_summary,
                                        !cycle.foot_support.empty())
                  << " no=" << cycle.foot_support_summary.no_support
                  << " all=" << cycle.foot_support_summary.all_support
                  << " short=" << cycle.foot_support_summary.stance_too_short
                  << " out=" << cycle.foot_support_summary.speed_outlier
                  << '\n';
      }
      reject("full cycle failed the strict gate");
      continue;
    }

    AnimationClip mirrored = Mirror(cycle);
    DeriveFootSupport(mirrored);
    DeriveMotionQuality(mirrored);
    if (!mirrored.motion_quality.ik_free_usable) {
      reject("mirrored full cycle failed the strict gate");
      continue;
    }

    const uint32_t first_id = first.id;
    const std::string first_name = first.name;
    const uint32_t second_id = second.id;
    const std::string second_name = second.name;
    cycle.id = first_id;
    cycle.name = first_name;
    RevalidateRepairedMetadata(cycle);
    mirrored.id = second_id;
    mirrored.name = second_name;
    RevalidateRepairedMetadata(mirrored);
    clips[index] = std::move(cycle);
    clips[static_cast<std::size_t>(other)] = std::move(mirrored);
    std::cerr << "AnimationLibrary: repaired gait cycle " << first_name
              << " (frames " << clips[index].frame_count << ")\n";
  }
}
}  // namespace animation_import
