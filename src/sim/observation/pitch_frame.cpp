#include "sim/observation/pitch_frame.hpp"


#include "sim/team/team.hpp"

PitchFrameTransform ToHomePitchFrame(const Team& team) {
  const int home_pitch_side = team.GetTeamSide() == football::model::TeamSide::Home
      ? -1 : 1;
  return PitchFrameTransform(team.GetDynamicSide() != home_pitch_side);
}

PitchFrameTransform FromHomePitchFrame(const Team& team) {
  return ToHomePitchFrame(team);
}
