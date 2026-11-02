#pragma once

#include "football/animation/clip.hpp"
#include "football/animation/skeleton.hpp"

// Import/verification workspace. Never stored in the runtime library or Player.
enum class FootPhase : std::uint8_t { Swing = 0, Landing = 1, Stance = 2, Takeoff = 3 };
struct FootSupportFrame {
  FootPhase left = FootPhase::Swing;
  FootPhase right = FootPhase::Swing;
};
inline bool FootIsSupporting(FootPhase phase) {
  return phase == FootPhase::Landing || phase == FootPhase::Stance ||
         phase == FootPhase::Takeoff;
}
struct FootSupportSummary {
  float landing_speed = 0.0f;
  float stance_speed = 0.0f;
  float takeoff_speed = 0.0f;
  float support_duty = 0.0f;
  float double_support_duty = 0.0f;
  float flight_duty = 0.0f;
  int shortest_support_run = 0;
  bool no_support = false;
  bool all_support = false;
  bool stance_too_short = false;
  bool speed_outlier = false;
};
inline bool FootSupportTrusted(const FootSupportSummary& summary, bool has_track) {
  return has_track && !summary.no_support && !summary.all_support &&
         !summary.stance_too_short && !summary.speed_outlier;
}
struct ImportedAnimationClip : AnimationClip {
  ImportedAnimationClip() = default;
  explicit ImportedAnimationClip(const AnimationClip& clip) : AnimationClip(clip) {}
  std::vector<FootSupportFrame> foot_support;
  float stance_speed = 0.0f;
  FootSupportSummary foot_support_summary;
};
inline constexpr float kFootDownHeight = 0.04f;
inline constexpr float kFootLiftHeight = 0.09f;
void DeriveFootSupport(ImportedAnimationClip& clip);
FootSupportFrame SampleFootSupport(const ImportedAnimationClip& clip, float frame_time);
void DeriveMotionQuality(ImportedAnimationClip& clip);
