#include "sim/observation/pitch_frame.hpp"

#include "sim/match/match.hpp"
#include "sim/team/team.hpp"

PitchFrameTransform ToHomePitchFrame(const Team& team) {
  const int home_pitch_side = team.GetTeamSide() == football::model::TeamSide::Home
      ? -1 : 1;
  return PitchFrameTransform(team.GetDynamicSide() != home_pitch_side);
}

PitchFrameTransform ToHomePitchFrame(const Match& match) {
  // Derive orientation from actual runtime sides, not MatchPhase: the referee
  // publishes SecondHalf one tick before the pending change of ends is applied.
  // This also handles reversed processing and any further change of ends.
  const int first_team = match.options().reverse_team_processing ? 1 : 0;
  return ToHomePitchFrame(*match.GetTeam(first_team));
}

PitchFrameTransform FromHomePitchFrame(const Team& team) {
  return ToHomePitchFrame(team);
}
