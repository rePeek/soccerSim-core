#ifndef FOOTBALL_AI_TEAM_AI_DECISION_FACTORY_HPP
#define FOOTBALL_AI_TEAM_AI_DECISION_FACTORY_HPP

#include <memory>

#include "sim/legacy_team_decision_factory.hpp"

// The default team decision implementation, chosen by the composition root
// (today src/env/game_env.cpp). Simulation never constructs it.
std::shared_ptr<const LegacyTeamDecisionFactory>
CreateDefaultTeamAIDecisionFactory();

#endif  // FOOTBALL_AI_TEAM_AI_DECISION_FACTORY_HPP
