#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "football/animation/clip.hpp"

// Shared immutable animation resource, loaded from a canonical GLB catalog.
// Binary .simanim artifacts are not accepted.
class AnimationLibrary {
 public:
  // Loads a catalog manifest: parses GLBs, generates movement variants/mirrors,
  // and derives support/quality data. Failure preserves the previous library.
  bool Load(const std::filesystem::path& manifest);

  const AnimationClip& Get(uint32_t id) const { return clips_.at(id); }

  const std::vector<AnimationClip>& Clips() const { return clips_; }

  std::size_t Size() const { return clips_.size(); }
  // RMA-1: a read-only census of the loaded clips' motion quality. Runtime
  // logging and tests use it; no gameplay decision reads it.
  // RM-0: the census that explains *why* ordinary movement clips do not pass
  // the strict gate. Counts are per reason, so a zero usable count is never
  // just "bad assets": the failing measurement is visible. A clip may appear in
  // several reason buckets; `movement` is the denominator.
  struct MotionQualityCensus {
    std::size_t total = 0;
    std::size_t tracked = 0;
    std::size_t ik_free_usable = 0;
    std::size_t stance_calibrated = 0;
    std::size_t support_slip = 0;
    std::size_t loop_discontinuous = 0;
    // RM-0 movement-clip diagnostics.
    std::size_t movement = 0;
    std::size_t movement_untracked = 0;
    std::size_t movement_support_untrusted = 0;
    std::size_t movement_loop_discontinuous = 0;
    std::size_t movement_stance_misaligned = 0;
    std::size_t movement_support_slip = 0;
    std::size_t movement_slow_root = 0;
    std::size_t movement_slow_stance = 0;
    std::size_t movement_rejected = 0;
  };
  MotionQualityCensus QualityCensus() const {
    MotionQualityCensus census;
    census.total = clips_.size();
    for (const AnimationClip& clip : clips_) {
      const bool tracked = clip.motion_quality.tracked;
      if (tracked) {
        ++census.tracked;
        if (clip.motion_quality.ik_free_usable) ++census.ik_free_usable;
        if (clip.motion_quality.stance_calibrated) ++census.stance_calibrated;
        if (!clip.motion_quality.support_consistent) ++census.support_slip;
        if (!clip.motion_quality.loop_continuous) ++census.loop_discontinuous;
      }
      if (clip.metadata.action_type != e_DefString_Movement) continue;
      ++census.movement;
      bool rejected = false;
      if (!clip.motion_quality.tracked) {
        ++census.movement_untracked;
        rejected = true;
      }
      if (!clip.motion_quality.support_trusted) {
        ++census.movement_support_untrusted;
        rejected = true;
      }
      if (!clip.motion_quality.loop_continuous) {
        ++census.movement_loop_discontinuous;
        rejected = true;
      }
      if (clip.motion_quality.stance_direction_alignment < 0.9f) {
        ++census.movement_stance_misaligned;
        rejected = true;
      }
      if (!clip.motion_quality.support_consistent) {
        ++census.movement_support_slip;
        rejected = true;
      }
      if (clip.motion_quality.root_speed <= 0.5f) {
        ++census.movement_slow_root;
        rejected = true;
      }
      if (clip.motion_quality.stance_speed <= 0.5f) {
        ++census.movement_slow_stance;
        rejected = true;
      }
      if (rejected && !clip.motion_quality.ik_free_usable) ++census.movement_rejected;
    }
    return census;
  }

 private:
  std::vector<AnimationClip> clips_;
};
