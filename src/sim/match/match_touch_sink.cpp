#include "sim/match/match_touch_sink.hpp"

#include "sim/match/match.hpp"
#include "sim/team/team.hpp"

namespace football::sim {

void MatchTouchSink::OnBallTouched(const BallTouchEvent& event) {
  event.team->NoteLastTouchPlayer(event.player, event.now, event.type);
  match_.SetLastTouchTeamID(event.team->GetID(), event.type, commands_);
}

}  // namespace football::sim
