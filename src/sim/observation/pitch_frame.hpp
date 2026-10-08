#ifndef FOOTBALL_SIM_PITCH_FRAME_HPP
#define FOOTBALL_SIM_PITCH_FRAME_HPP

#include "foundation/math/vector3.hpp"

class Team;

// Runtime-to-policy rotation around the pitch centre. Position and direction
// share the same rotation; height/vertical velocity and vector lengths do not
// change. A 180-degree rotation is its own inverse. This is a transient adapter,
// never stored as another authoritative side/phase flag.
class PitchFrameTransform {
 public:
  explicit constexpr PitchFrameTransform(bool mirrored) : mirrored_(mirrored) {}

  blunted::Vector3 Position(blunted::Vector3 value) const {
    if (mirrored_) value.Mirror();
    return value;
  }
  blunted::Vector3 Direction(blunted::Vector3 value) const {
    if (mirrored_) value.Mirror();
    return value;
  }

 private:
  bool mirrored_;
};

// An actor's current processing frame -> fixed policy frame (Home defends -X).
PitchFrameTransform ToHomePitchFrame(const Team& team);
// The between-tick ball frame is derived from the first processing roster by
// the composition root; this header only exposes the per-team frame.
// Fixed policy controls -> the actor's current processing frame.
PitchFrameTransform FromHomePitchFrame(const Team& team);

#endif
