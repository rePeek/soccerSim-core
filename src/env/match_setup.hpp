#ifndef _HPP_FOOTBALL_MATCH_SETUP
#define _HPP_FOOTBALL_MATCH_SETUP

#include <memory>
#include <vector>

#include "data/matchdata.hpp"

// Pure match-start data. It deliberately contains no GUI objects so the
// simulation can be configured without MenuTask or WindowManager.
struct ControllerSetup {
  int controller_id = 0;
  int side = 0;  // -1 = left, 0 = builtin AI, 1 = right
};

struct MatchSetup {
  std::vector<ControllerSetup> controllers;
  std::unique_ptr<MatchData> match_data;
};

#endif
