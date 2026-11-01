// Foot support derivation: which foot of a baked clip carries weight, and how
// fast it travels backwards inside the clip.
//
// Everything here is derived from the clip's own pose track and the declared
// 14-bone model. It runs once per clip when the library loads, so the runtime
// pays a table lookup per tick and the artifact format does not change.

#include "football/animation/foot_support.hpp"

#include <algorithm>
#include <cmath>

namespace {

using football::math::Quaternion;

// Mean root-track speed over the clip, the reference the support derivation is
// checked against. Same definition `LocomotionClipReferenceSpeed` uses, kept
// local so the animation module does not depend on the Player header.
float LocomotionRootSpeedForSummary(const AnimationClip& clip) {
  const std::size_t samples = clip.root_positions.size();
  if (samples < 2) return 0.0f;
  const auto d = clip.root_positions.back() - clip.root_positions.front();
  const float dist = std::sqrt(d.coords[0] * d.coords[0] + d.coords[1] * d.coords[1]);
  const float span_s = static_cast<float>(samples - 1) * 0.01f;
  return span_s > 0.0f ? dist / span_s : 0.0f;
}
using football::math::Vector3;

// World-space ankle position of one leg for one frame, relative to the player
// root, with no base yaw: only the clip's own rotations matter here.
Vector3 AnklePosition(const PoseFrame& pose, BodyPart thigh, BodyPart knee,
                      BodyPart ankle) {
  Quaternion body_rotation = pose.orientations[BodyPart::body];
  const Vector3 body_origin = PlayerBindOffset(BodyPart::body);
  const Vector3 hip = body_origin +
                      body_rotation * PlayerBindOffset(thigh);
  const Quaternion thigh_rotation =
      (body_rotation * pose.orientations[thigh]).GetNormalized();
  const Vector3 knee_position = hip + thigh_rotation * PlayerBindOffset(knee);
  const Quaternion knee_rotation =
      (thigh_rotation * pose.orientations[knee]).GetNormalized();
  return knee_position + knee_rotation * PlayerBindOffset(ankle);
}

// Hysteresis state machine over one foot's height track. `down` is the contact
// band, `up` the release band; a cyclic clip is walked twice so the state is
// steady when the first frame is classified.
void ClassifyContacts(const std::vector<float>& heights, float enter_height,
                      float leave_height, std::vector<bool>& down) {
  const std::size_t frames = heights.size();
  down.assign(frames, false);
  if (frames == 0) return;
  // Start from the first frame's own height so a non-cyclic clip is not biased.
  bool contact = heights.front() <= enter_height;
  for (int pass = 0; pass < 2; ++pass) {
    for (std::size_t i = 0; i < frames; ++i) {
      const float height = heights[i];
      if (contact) {
        if (height > leave_height) contact = false;
      } else {
        if (height <= enter_height) contact = true;
      }
      down[i] = contact;
    }
  }
}

// Landing on the first frame of a contact run, Takeoff on the last, Stance in
// between. Runs wrap around the cycle, so a stance that crosses the loop
// boundary still gets its two edges.
void AssignPhases(const std::vector<bool>& down,
                  std::vector<FootPhase>& phases) {
  const std::size_t frames = down.size();
  phases.assign(frames, FootPhase::Swing);
  if (frames == 0) return;
  bool any_down = false;
  for (const bool contact : down) any_down = any_down || contact;
  if (!any_down) return;
  // Every frame down means the foot never leaves: one long contact.
  bool all_down = true;
  for (const bool contact : down) all_down = all_down && contact;
  if (all_down) {
    for (auto& phase : phases) phase = FootPhase::Stance;
    return;
  }
  for (std::size_t i = 0; i < frames; ++i) {
    if (!down[i]) continue;
    const std::size_t previous = (i + frames - 1) % frames;
    const std::size_t next = (i + 1) % frames;
    if (!down[previous] && !down[next]) {
      // A one-frame contact: it lands and takes off in the same frame.
      phases[i] = FootPhase::Stance;
    } else if (!down[previous]) {
      phases[i] = FootPhase::Landing;
    } else if (!down[next]) {
      phases[i] = FootPhase::Takeoff;
    } else {
      phases[i] = FootPhase::Stance;
    }
  }
}

}  // namespace

void DeriveFootSupport(AnimationClip& clip) {
  clip.foot_support.clear();
  clip.stance_speed = 0.0f;
  const std::size_t frames = std::min(clip.poses.size(), clip.root_positions.size());
  if (frames < 2) return;

  std::vector<Vector3> left_ankles;
  std::vector<Vector3> right_ankles;
  left_ankles.reserve(frames);
  right_ankles.reserve(frames);
  for (std::size_t f = 0; f < frames; ++f) {
    left_ankles.push_back(AnklePosition(clip.poses[f], BodyPart::left_thigh,
                                        BodyPart::left_knee,
                                        BodyPart::left_ankle));
    right_ankles.push_back(AnklePosition(clip.poses[f], BodyPart::right_thigh,
                                         BodyPart::right_knee,
                                         BodyPart::right_ankle));
  }

  // The clip's own ground level: the lowest either ankle ever gets.
  float ground = left_ankles.front().coords[2];
  for (const auto& ankle : left_ankles) ground = std::min(ground, ankle.coords[2]);
  for (const auto& ankle : right_ankles) ground = std::min(ground, ankle.coords[2]);

  std::vector<float> left_heights;
  std::vector<float> right_heights;
  left_heights.reserve(frames);
  right_heights.reserve(frames);
  for (std::size_t f = 0; f < frames; ++f) {
    left_heights.push_back(left_ankles[f].coords[2] - ground);
    right_heights.push_back(right_ankles[f].coords[2] - ground);
  }

  std::vector<bool> left_down;
  std::vector<bool> right_down;
  ClassifyContacts(left_heights, kFootDownHeight, kFootLiftHeight, left_down);
  ClassifyContacts(right_heights, kFootDownHeight, kFootLiftHeight, right_down);

  std::vector<FootPhase> left_phases;
  std::vector<FootPhase> right_phases;
  AssignPhases(left_down, left_phases);
  AssignPhases(right_down, right_phases);

  clip.foot_support.resize(frames);
  for (std::size_t f = 0; f < frames; ++f) {
    clip.foot_support[f].left = left_phases[f];
    clip.foot_support[f].right = right_phases[f];
  }

  // The stride speed a clip implies: how fast the supporting foot travels
  // backwards inside the clip itself. Measured per phase, because landing,
  // stance and takeoff do not move the foot at the same rate - and because a
  // swing phase must not dilute it. The root track only says how fast the clip
  // *intends* to travel; authored clips disagree with it.
  constexpr float kFrameSeconds = 0.01f;
  struct SpeedAccumulator {
    float total = 0.0f;
    int samples = 0;
  };
  SpeedAccumulator landing;
  SpeedAccumulator stance;
  SpeedAccumulator takeoff;
  int support_frames = 0;
  int double_support_frames = 0;
  int flight_frames = 0;
  int shortest_run = 0;
  const auto accumulate = [&](const std::vector<Vector3>& ankles,
                              const std::vector<FootPhase>& phases) {
    int run = 0;
    for (std::size_t f = 0; f < frames; ++f) {
      const std::size_t next = (f + 1) % frames;
      const FootPhase phase = phases[f];
      if (FootIsSupporting(phase)) ++run;
      if (phase == FootPhase::Swing) {
        // A finished contact run.
        if (run > 0) {
          shortest_run = shortest_run == 0 ? run : std::min(shortest_run, run);
        }
        run = 0;
        continue;
      }
      if (run > 0 && f + 1 == frames) {
        shortest_run = shortest_run == 0 ? run : std::min(shortest_run, run);
      }
      // Clips face -Y in their own frame, so backwards is +Y.
      const float backward = ankles[next].coords[1] - ankles[f].coords[1];
      const float speed = backward / kFrameSeconds;
      if (speed <= 0.0f) continue;  // a foot sliding forwards is not stance data
      SpeedAccumulator* bucket = phase == FootPhase::Landing
                                     ? &landing
                                     : (phase == FootPhase::Takeoff ? &takeoff
                                                                    : &stance);
      bucket->total += speed;
      ++bucket->samples;
    }
  };
  accumulate(left_ankles, left_phases);
  accumulate(right_ankles, right_phases);

  FootSupportSummary& summary = clip.foot_support_summary;
  const auto mean = [](const SpeedAccumulator& accumulator) {
    return accumulator.samples > 0
               ? accumulator.total / static_cast<float>(accumulator.samples)
               : 0.0f;
  };
  summary.landing_speed = mean(landing);
  summary.stance_speed = mean(stance);
  summary.takeoff_speed = mean(takeoff);
  for (std::size_t f = 0; f < frames; ++f) {
    const bool left = FootIsSupporting(clip.foot_support[f].left);
    const bool right = FootIsSupporting(clip.foot_support[f].right);
    if (left || right) ++support_frames;
    if (left && right) ++double_support_frames;
    if (!left && !right) ++flight_frames;
  }
  const float frame_share = 1.0f / static_cast<float>(frames);
  summary.support_duty = static_cast<float>(support_frames) * frame_share;
  summary.double_support_duty = static_cast<float>(double_support_frames) * frame_share;
  summary.flight_duty = static_cast<float>(flight_frames) * frame_share;
  summary.shortest_support_run = shortest_run;

  // Quality: a Movement clip needs real, alternating contact; a very short
  // contact is not a step; and a stance foot that disagrees with the root track
  // by a wide margin means the phases were read wrong.
  const bool movement = clip.metadata.action_type == e_DefString_Movement;
  summary.no_support = support_frames == 0;
  // Both feet down for the whole clip: a stance, not a gait.
  summary.all_support = double_support_frames == frames;
  summary.stance_too_short = movement && shortest_run > 0 && shortest_run < 2;
  const float root_speed = LocomotionRootSpeedForSummary(clip);
  const float stance_speed = summary.stance_speed > 0.0f
                                 ? summary.stance_speed
                                 : clip.stance_speed;
  summary.speed_outlier =
      movement && root_speed > 1.5f &&
      (stance_speed < 0.35f * root_speed || stance_speed > 1.6f * root_speed + 1.0f);

  // The single number playback follows: the stance phase, falling back to the
  // whole contact (a clip whose stance run is a single frame still has a value).
  clip.stance_speed = stance_speed;
}

FootSupportFrame SampleFootSupport(const AnimationClip& clip, float frame_time) {
  FootSupportFrame frame;
  if (clip.foot_support.empty()) return frame;
  const float last = static_cast<float>(clip.foot_support.size() - 1);
  if (!(frame_time >= 0.0f)) frame_time = 0.0f;  // also catches NaN
  if (frame_time > last) frame_time = last;
  const auto index = static_cast<std::size_t>(std::lround(frame_time));
  return clip.foot_support[std::min(index, clip.foot_support.size() - 1)];
}
