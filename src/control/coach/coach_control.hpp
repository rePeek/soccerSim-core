#ifndef FOOTBALL_CONTROL_COACH_COACH_CONTROL_HPP
#define FOOTBALL_CONTROL_COACH_COACH_CONTROL_HPP

#include "control/coach/coach_control_context.hpp"
#include "control/coach/team_plan.hpp"

class CoachControl {
 public:
  virtual ~CoachControl() = default;

  virtual TeamPlan Decide(const CoachControlContext& context) = 0;
  virtual void Reset() {}
};

#endif  // FOOTBALL_CONTROL_COACH_COACH_CONTROL_HPP
