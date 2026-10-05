#ifndef FOOTBALL_SIM_MATCH_CONFIG_HPP
#define FOOTBALL_SIM_MATCH_CONFIG_HPP

#include <memory>
#include <vector>

#include "data/matchdata.hpp"
#include "controller/controller_id.hpp"
#include "sim/pitch.hpp"

// Authoritative match-start configuration. It deliberately contains no GUI
// objects, so simulation can be configured without MenuTask or WindowManager.
struct ControllerSetup {
  ControllerId controller_id = 0;
  int side = 0;  // -1 = left, 0 = builtin AI, 1 = right
};

struct MatchConfig {
  Pitch pitch;
  std::vector<ControllerSetup> controllers;
  std::unique_ptr<MatchData> match_data;
  // Scales how fast match time advances. Replaces the legacy
  // Properties["match_duration"] lookup that Match used to read.
  float match_duration = 0.027f;
  // Match-level rule switches and per-side AI difficulty, snapshotted from
  // the environment scenario so the simulation no longer reads the ambient
  // ScenarioConfig singleton. Defaults match ScenarioConfig.
  bool reverse_team_processing = false;
  bool use_magnet = true;
  float left_team_difficulty = 1.0f;
  float right_team_difficulty = 0.6f;
};

#endif  // FOOTBALL_SIM_MATCH_CONFIG_HPP
