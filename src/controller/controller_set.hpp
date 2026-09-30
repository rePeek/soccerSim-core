#ifndef FOOTBALL_CONTROLLER_CONTROLLER_SET_HPP
#define FOOTBALL_CONTROLLER_CONTROLLER_SET_HPP

#include <vector>

#include "controller/controller_input.hpp"
#include "controller/controller_id.hpp"

// Non-owning registry assembled by the environment. Simulation receives this
// abstract input set rather than concrete device implementations.
class ControllerSet {
 public:
  void Add(ControllerInput& controller) { controllers_.push_back(&controller); }
  ControllerInput* at(ControllerId id) const { return controllers_.at(id); }

  const std::vector<ControllerInput*>& controllers() const {
    return controllers_;
  }

 private:
  std::vector<ControllerInput*> controllers_;
};

#endif  // FOOTBALL_CONTROLLER_CONTROLLER_SET_HPP
