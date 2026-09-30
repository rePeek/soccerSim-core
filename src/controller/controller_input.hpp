#ifndef FOOTBALL_CONTROLLER_CONTROLLER_INPUT_HPP
#define FOOTBALL_CONTROLLER_CONTROLLER_INPUT_HPP

#include "foundation/math/vector3.hpp"

// Input state consumed by the simulation. Implementations may be driven by a
// local device, an external protocol, or a replay, but the simulation only
// reads this controller-domain surface.
enum e_ButtonFunction {
  e_ButtonFunction_LongPass,
  e_ButtonFunction_HighPass,
  e_ButtonFunction_ShortPass,
  e_ButtonFunction_Shot,
  e_ButtonFunction_KeeperRush,
  e_ButtonFunction_Sliding,
  e_ButtonFunction_Pressure,
  e_ButtonFunction_TeamPressure,
  e_ButtonFunction_Switch,
  e_ButtonFunction_Sprint,
  e_ButtonFunction_Dribble,
  e_ButtonFunction_Size
};

class ControllerInput {
 public:
  virtual ~ControllerInput() = default;

  virtual bool GetButton(e_ButtonFunction button_function) = 0;
  virtual bool GetPreviousButtonState(e_ButtonFunction button_function) = 0;
  virtual blunted::Vector3 GetDirection() = 0;
  virtual blunted::Vector3 GetOriginalDirection() = 0;
  virtual bool Disabled() const = 0;
  virtual void ResetNotSticky() = 0;
  virtual void Mirror(float mirror) = 0;
  virtual int GetPlayerColorIndex() const = 0;
};

#endif  // FOOTBALL_CONTROLLER_CONTROLLER_INPUT_HPP
