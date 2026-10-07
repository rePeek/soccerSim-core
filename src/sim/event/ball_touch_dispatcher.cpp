#include "sim/event/ball_touch_dispatcher.hpp"

#include "sim/player/player.hpp"
#include "sim/rules/referee.hpp"
#include "sim/team/team.hpp"

namespace football::sim::event {

void DispatchBallTouch(const BallTouchEvent& event, TouchState& touches,
                       rules::BallTouchFacts facts, Referee& referee,
                       rules::RuleCommandSink& commands) {
  // Player-local age/type plus event-owned identity records; Team holds no touch state.
  event.player->SetLastTouchTick(event.now);
  event.player->SetLastTouchType(event.type);
  touches.Record(event.team->GetID(), event.player->GetID(), event.type);
  facts.touch_player = event.player;
  facts.touch_team_id = event.team->GetID();
  facts.touch_team = event.team;
  referee.BallTouched(facts, commands);
}

} // namespace football::sim::event
