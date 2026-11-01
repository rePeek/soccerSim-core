#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "football/animation/clip.hpp"

// Shared immutable animation resource, loaded from a canonical GLB catalog.
// Binary .simanim artifacts are not accepted.
class AnimationLibrary {
 public:
  // Alias for LoadCatalog; the input is always a catalog manifest.
  bool Load(const std::filesystem::path& manifest);
  // Parses GLBs, generates movement variants/mirrors, derives support data.
  // Failure returns false and preserves the previously loaded resource.
  bool LoadCatalog(const std::filesystem::path& manifest);

  const AnimationClip& Get(uint32_t id) const { return clips_.at(id); }

  const std::vector<AnimationClip>& Clips() const { return clips_; }

  std::size_t Size() const { return clips_.size(); }
  // Stable resource provenance for offline snapshot pose lookup.
  std::uint64_t ContentHash() const { return content_hash_; }
  // RMA-1: a read-only census of the loaded clips' motion quality. Runtime
  // logging and tests use it; no gameplay decision reads it.
  struct MotionQualityCensus {
    std::size_t total = 0;
    std::size_t tracked = 0;
    std::size_t ik_free_usable = 0;
    std::size_t stance_calibrated = 0;
    std::size_t support_slip = 0;
    std::size_t loop_discontinuous = 0;
  };
  MotionQualityCensus QualityCensus() const {
    MotionQualityCensus census;
    census.total = clips_.size();
    for (const AnimationClip& clip : clips_) {
      if (!clip.motion_quality.tracked) continue;
      ++census.tracked;
      if (clip.motion_quality.ik_free_usable) ++census.ik_free_usable;
      if (clip.motion_quality.stance_calibrated) ++census.stance_calibrated;
      if (!clip.motion_quality.support_consistent) ++census.support_slip;
      if (!clip.motion_quality.loop_continuous) ++census.loop_discontinuous;
    }
    return census;
  }

 private:
  std::vector<AnimationClip> clips_;
  std::uint64_t content_hash_ = 0;
};
