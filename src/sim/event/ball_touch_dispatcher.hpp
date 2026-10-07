#ifndef FOOTBALL_SIM_EVENT_BALL_TOUCH_DISPATCHER_HPP
#define FOOTBALL_SIM_EVENT_BALL_TOUCH_DISPATCHER_HPP

#include "sim/event/ball_touch.hpp"
#include "sim/event/touch_state.hpp"
#include "sim/rules/ball_touch_facts.hpp"
#include "sim/rules/rule_command_sink.hpp"

class Referee;

namespace football::sim::event {

// Bookkeeping then immediate rule consequences. Rule evaluation's now is supplied
// separately from the actor touch timestamp (diagnostics may deliberately differ).
void DispatchBallTouch(const BallTouchEvent& event, TouchState& touches,
                       rules::BallTouchFacts facts, Referee& referee,
                       rules::RuleCommandSink& commands);

} // namespace football::sim::event
#endif
