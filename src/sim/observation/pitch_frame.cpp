#include "sim/observation/pitch_frame.hpp"


#include "sim/team/team.hpp"

PitchFrameTransform ToHomePitchFrame(football::sim::TeamSide side, int dynamic_side) {
  const int home_pitch_side = side == football::sim::TeamSide::Home ? -1 : 1;
  return PitchFrameTransform(dynamic_side != home_pitch_side);
}

PitchFrameTransform ToHomePitchFrame(const Team& team) {
  return ToHomePitchFrame(team.GetTeamSide(), team.GetDynamicSide());
}

PitchFrameTransform FromHomePitchFrame(const Team& team) {
  return ToHomePitchFrame(team.GetTeamSide(), team.GetDynamicSide());
}

PitchFrameTransform FromHomePitchFrame(football::sim::TeamSide side, int dynamic_side) {
  return ToHomePitchFrame(side, dynamic_side);
}

