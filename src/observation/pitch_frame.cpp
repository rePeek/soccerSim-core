#include "observation/pitch_frame.hpp"



PitchFrameTransform ToHomePitchFrame(football::sim::TeamSide side, int dynamic_side) {
  const int home_pitch_side = side == football::sim::TeamSide::Home ? -1 : 1;
  return PitchFrameTransform(dynamic_side != home_pitch_side);
}


PitchFrameTransform FromHomePitchFrame(football::sim::TeamSide side, int dynamic_side) {
  return ToHomePitchFrame(side, dynamic_side);
}

