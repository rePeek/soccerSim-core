#ifndef FOOTBALL_CONTROLLER_GRF_GRF_ACTION_CONTROLLER_HPP
#define FOOTBALL_CONTROLLER_GRF_GRF_ACTION_CONTROLLER_HPP

#include "controller/external_controller.hpp"
#include "controller/grf/action.hpp"

// Translates the GRF action protocol into controller input state. It has no
// simulation, environment, lifecycle, or player-selection knowledge.
class GrfActionController {
 public:
  static void Apply(Action action, ExternalController& controller);
  static bool IsStickyActionActive(Action action,
                                   ExternalController& controller);
};

#endif  // FOOTBALL_CONTROLLER_GRF_GRF_ACTION_CONTROLLER_HPP
