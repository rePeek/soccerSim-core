// Pure pitch frame transforms over foundation math. This header defines the
// only coordinate conversions Runtime, observation, control execution and AI
// are allowed to share; it never depends on Runtime, a Team, a Player or the
// MatchPhase, so "how a coordinate changes" stays separate from "when an end
// change happens" (Runtime's job).
//
// Two named spaces exist here:
//
// - Home Pitch Frame: the canonical, externally published match frame. Home
//   always defends -X and Away always defends +X, in both halves, whichever
//   team the between-tick execution frame belongs to. WorldState, Snapshot,
//   TacticalBoard anchors and the restart planner all speak it.
// - Team Attacking Frame: one team's own view. Its own goal is at -X and it
//   attacks +X, independent of Home/Away identity and half. Tactical intent
//   belongs here so the same formation/width/role data needs no per-half copy.
//
// Every transform below is a proper 180-degree rotation about Z: it negates X
// and Y and leaves Z untouched. It is an involution (its own inverse) and an
// isometry, so a position, a velocity, an acceleration or a facing all map by
// exactly the same operation and no reflection (handedness) is introduced.
#ifndef FOOTBALL_PITCH_PITCH_FRAME_HPP
#define FOOTBALL_PITCH_PITCH_FRAME_HPP

#include "football/foundation/math/vector3.hpp"

// The end of the pitch a team defends, named without Home/Away or a half. It is
// the bridge from the runtime's ±1 side convention to the pure transform below.
enum class DefendingEnd {
  NegativeX,
  PositiveX,
};

// The runtime's ±1 side convention -> DefendingEnd. Runtime stores the
// direction a team defends as -1 (defends -X) or +1 (defends +X); this is the
// only place that convention is translated, so its meaning has one owner.
constexpr DefendingEnd DefendingEndOf(int side) {
  return side < 0 ? DefendingEnd::NegativeX : DefendingEnd::PositiveX;
}

class PitchFrameTransform {
 public:
  explicit constexpr PitchFrameTransform(bool rotated) : rotated_(rotated) {}

  // Position, velocity, acceleration or facing: all transform identically
  // under this 180-degree rotation. Keeping one entry point stops call sites
  // from inventing a differently-oriented "direction" conversion.
  football::math::Vector3 Position(football::math::Vector3 value) const {
    if (rotated_) value.Mirror();
    return value;
  }
  football::math::Vector3 Direction(football::math::Vector3 value) const {
    if (rotated_) value.Mirror();
    return value;
  }

 private:
  bool rotated_;
};

// Actor processing frame -> fixed Home Pitch Frame. `home` is the caller's
// TeamSide::Home membership (an identity, not a direction) and `dynamic_side`
// the runtime ±1 execution direction; the two disagree exactly when the
// actor's processing frame is already mirrored relative to the canonical one.
// The transform is its own inverse, so the same call also goes back.
inline PitchFrameTransform ToHomePitchFrame(bool home, int dynamic_side) {
  const int home_pitch_side = home ? -1 : 1;
  return PitchFrameTransform(dynamic_side != home_pitch_side);
}

inline PitchFrameTransform FromHomePitchFrame(bool home, int dynamic_side) {
  return ToHomePitchFrame(home, dynamic_side);
}

// Fixed Stadium/Home Pitch Frame -> the attacking frame of the team defending
// `defending_end`. A team already defending -X needs no rotation; defending +X
// rotates the whole plane so both its own goal and its attack direction land in
// the canonical attacking frame. Self-inverse.
constexpr PitchFrameTransform ToAttackingFrame(DefendingEnd defending_end) {
  return PitchFrameTransform(defending_end == DefendingEnd::PositiveX);
}

constexpr PitchFrameTransform FromAttackingFrame(DefendingEnd defending_end) {
  return ToAttackingFrame(defending_end);
}

// Named Stadium <-> published Home Pitch Frame conversions. The published
// frame is Home's attacking frame (Home always defends -X), so this is the
// same rotation as ToAttackingFrame(home_defends). Self-inverse, so both
// directions use the same factory; the two names exist so callers state which
// way they mean. `home_defends` is the *home* team's end, never a side id.
constexpr PitchFrameTransform StadiumToHomePitch(DefendingEnd home_defends) {
  return ToAttackingFrame(home_defends);
}

constexpr PitchFrameTransform HomePitchToStadium(DefendingEnd home_defends) {
  return ToAttackingFrame(home_defends);
}

#endif  // FOOTBALL_PITCH_PITCH_FRAME_HPP
