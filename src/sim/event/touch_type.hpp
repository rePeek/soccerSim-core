#ifndef FOOTBALL_SIM_EVENT_TOUCH_TYPE_HPP
#define FOOTBALL_SIM_EVENT_TOUCH_TYPE_HPP

// Touch classification belongs to the event/referee domain, not to Ball.
// Ball only receives physical impulses; it never classifies who or why a
// touch happened.

enum e_TouchType {
  e_TouchType_Intentional_Kicked,  // goalies can't touch this
  e_TouchType_Intentional_Nonkicked,  // headers and such
  e_TouchType_Accidental,  // collisions
  e_TouchType_None,
  e_TouchType_SIZE
};

#endif  // FOOTBALL_SIM_EVENT_TOUCH_TYPE_HPP