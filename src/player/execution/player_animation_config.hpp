// Per-player animation configuration: one light tuning record per standard clip,
// built once when the Player is created and constant for the whole match.
//
// This is the boundary the architecture needs: the AnimationLibrary stays an
// immutable, globally shared resource of standard clips, and everything that
// makes a particular player use those clips differently lives here. Nothing in
// this structure copies a skeleton frame, so 22 players x ~200 clips is a few
// tens of kilobytes.
//
// The configuration cannot move a Player: stride and cadence only change how the
// same achieved motion is *performed*. World displacement stays PlayerMotion's,
// and action clips (shot/pass/trip) keep their action clock.
#ifndef FOOTBALL_SIM_PLAYER_ANIMATION_CONFIG_HPP
#define FOOTBALL_SIM_PLAYER_ANIMATION_CONFIG_HPP

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "football/animation/library.hpp"
#include "football/animation/types.hpp"
#include "football/player/player_animation_style.hpp"
#include "football/player/player_body_profile.hpp"
#include "football/player/player_definition.hpp"

// One clip, as this player performs it.
struct ClipTuning {
  float stride_preference = 1.0f;
  float cadence_preference = 1.0f;
  float stride_response = 0.5f;
  float pelvis_bob_scale = 1.0f;
  float torso_sway_scale = 1.0f;
  float arm_swing_scale = 1.0f;
};

// Per-clip tuning, indexed by the standard clip's id.
struct PlayerAnimationConfig {
  std::vector<ClipTuning> clips;

  // Out-of-range ids get the neutral tuning instead of throwing: a config is
  // built from a specific library, but a caller must not be able to crash a tick
  // with an id from another one.
  const ClipTuning& Get(AnimationId id) const {
    if (id < 0 || static_cast<std::size_t>(id) >= clips.size()) {
      static const ClipTuning neutral{};
      return neutral;
    }
    return clips[static_cast<std::size_t>(id)];
  }
  bool empty() const { return clips.empty(); }
  std::size_t size() const { return clips.size(); }
};

// Caller-supplied per-clip overrides, keyed by the clip's *stable name*, never by
// the numeric id: `AnimationLibrary::Get(id)` follows the load order, so ids are
// not a durable identity across bakes.
using AnimationClipOverrideMap = std::unordered_map<std::string, ClipTuning>;

// Builds the configuration once, deterministically:
//
//   PlayerDefinition.animation_style (or neutral)
//     -> sanitized AnimationStyle
//     -> one ClipTuning per clip in the given library
//     -> optional name-keyed overrides resolved to clip ids
//
// `body` is accepted so a future per-build modulation can live here; a style is
// deliberately independent of the skeleton (height is a scale, not a style).
PlayerAnimationConfig BuildPlayerAnimationConfig(
    const football::player::PlayerDefinition& definition,
    const football::player::PlayerBodyProfile& body,
    const AnimationLibrary& library,
    const AnimationClipOverrideMap* overrides = nullptr);

#endif  // FOOTBALL_SIM_PLAYER_ANIMATION_CONFIG_HPP
