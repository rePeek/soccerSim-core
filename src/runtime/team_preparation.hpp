#pragma once

#include <array>
#include <vector>
#include "runtime/team_config.hpp"
#include "football/runtime/match_config.hpp"

namespace football::sim {
// Init-local staging only. All definitions are consumed or discarded before
// Init returns; the live Team stores only TeamConfig and actual Players.
struct PreparedTeam : TeamConfig {
  std::vector<football::player::PlayerDefinition> players;
  bool explicit_lineup = false;  // legacy vs explicit actor construction mode
};
// Pure validation/default binding, before appearance or simulation RNG draws.
PreparedTeam PrepareTeam(const TeamSetup& setup);
void ValidateTeam(const PreparedTeam& team);
void ValidateMatchTeams(const std::array<PreparedTeam, 2>& teams);
}  // namespace football::sim
