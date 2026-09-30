#ifndef FOOTBALL_SIM_MATCH_CONFIG_HPP
#define FOOTBALL_SIM_MATCH_CONFIG_HPP

#include <memory>
#include <vector>

#include "data/matchdata.hpp"

// Authoritative match-start configuration. It deliberately contains no GUI
// objects, so simulation can be configured without MenuTask or WindowManager.
struct ControllerSetup {
  int controller_id = 0;
  int side = 0;  // -1 = left, 0 = builtin AI, 1 = right
};

struct MatchConfig {
  std::vector<ControllerSetup> controllers;
  std::unique_ptr<MatchData> match_data;
};

#endif  // FOOTBALL_SIM_MATCH_CONFIG_HPP
