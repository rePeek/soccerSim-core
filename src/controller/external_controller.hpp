#ifndef FOOTBALL_CONTROLLER_EXTERNAL_CONTROLLER_HPP
#define FOOTBALL_CONTROLLER_EXTERNAL_CONTROLLER_HPP

#include "foundation/math/vector3.hpp"

// Controller-domain input primitives. The world consumes these inputs; this
// layer owns no lifecycle or world authority.
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

class ExternalController {
 public:
  virtual ~ExternalController() = default;

  virtual bool GetButton(e_ButtonFunction button_function) = 0;
  virtual blunted::Vector3 GetOriginalDirection() = 0;
  virtual void SetButton(e_ButtonFunction button_function, bool state) = 0;
  virtual void SetDirection(const blunted::Vector3& direction) = 0;
  virtual void SetDisabled(bool disabled) = 0;
};

#endif  // FOOTBALL_CONTROLLER_EXTERNAL_CONTROLLER_HPP
