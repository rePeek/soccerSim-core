#pragma once

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
