// Pure pitch frame transform: an actor's processing frame <-> the fixed
// home-pitch frame (Home defends -X). It is geometry only and depends on
// foundation math, never on Runtime or an actor. `home` is the caller's
// TeamSide::Home membership; the transform is its own inverse.
#ifndef FOOTBALL_PITCH_PITCH_FRAME_HPP
#define FOOTBALL_PITCH_PITCH_FRAME_HPP

#include "football/foundation/math/vector3.hpp"

class PitchFrameTransform {
 public:
  explicit constexpr PitchFrameTransform(bool mirrored) : mirrored_(mirrored) {}
  football::math::Vector3 Position(football::math::Vector3 value) const {
    if (mirrored_) value.Mirror();
    return value;
  }
  football::math::Vector3 Direction(football::math::Vector3 value) const {
    if (mirrored_) value.Mirror();
    return value;
  }

 private:
  bool mirrored_;
};

inline PitchFrameTransform ToHomePitchFrame(bool home, int dynamic_side) {
  const int home_pitch_side = home ? -1 : 1;
  return PitchFrameTransform(dynamic_side != home_pitch_side);
}

inline PitchFrameTransform FromHomePitchFrame(bool home, int dynamic_side) {
  return ToHomePitchFrame(home, dynamic_side);
}

#endif  // FOOTBALL_PITCH_PITCH_FRAME_HPP
