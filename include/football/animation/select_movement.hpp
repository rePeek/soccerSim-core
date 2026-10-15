#ifndef FOOTBALL_ANIMATION_SELECT_MOVEMENT_HPP
#define FOOTBALL_ANIMATION_SELECT_MOVEMENT_HPP

#include <cstdint>
#include <span>

#include "football/animation/types.hpp"

// ANIM-1 A6.3b.1 — the final Movement pick rule, extracted pure.
//
// The legacy Movement selection keeps a stable-ordered candidate list (after
// `CrudeSelection` + the legacy direction/velocity filters + the stable sort
// chain) and prefers the motion-driven strategy's clip only when it is already
// a legal candidate; otherwise the legacy first candidate plays (the fallback).
// This mirrors that rule exactly, with no Player/Humanoid/Simulation/Ball/RNG
// dependency and no change to ordering or floating-point semantics.

inline AnimationId ResolveMovementPick(std::span<const int> legacy_ordered_candidates,
                                       AnimationId preferred) {
  if (legacy_ordered_candidates.empty()) return preferred;
  for (const int id : legacy_ordered_candidates) {
    if (id == static_cast<int>(preferred)) return preferred;
  }
  return static_cast<AnimationId>(legacy_ordered_candidates.front());
}

#endif  // FOOTBALL_ANIMATION_SELECT_MOVEMENT_HPP
