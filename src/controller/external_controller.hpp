#ifndef FOOTBALL_CONTROLLER_EXTERNAL_CONTROLLER_HPP
#define FOOTBALL_CONTROLLER_EXTERNAL_CONTROLLER_HPP

#include "controller/controller_input.hpp"

// Mutable input endpoint used by external adapters. Simulation consumes only
// the ControllerInput base; protocol adapters may update this richer surface.
class ExternalController : public ControllerInput {
 public:
  ~ExternalController() override = default;

  virtual void SetButton(e_ButtonFunction button_function, bool state) = 0;
  virtual void SetDirection(const blunted::Vector3& direction) = 0;
  virtual void SetDisabled(bool disabled) = 0;
};

#endif  // FOOTBALL_CONTROLLER_EXTERNAL_CONTROLLER_HPP
