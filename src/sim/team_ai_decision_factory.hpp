#ifndef FOOTBALL_SIM_TEAM_AI_DECISION_FACTORY_HPP
#define FOOTBALL_SIM_TEAM_AI_DECISION_FACTORY_HPP

#include <memory>

#include "sim/legacy_team_decision_factory.hpp"

// TRANSITIONAL. Today's default team decision, defined beside the
// implementation it creates. It moves with TeamAIController into src/ai, and
// the composition root will inject it instead of Simulation constructing a
// default.
std::shared_ptr<const LegacyTeamDecisionFactory> MakeTeamAiDecisionFactory();

#endif  // FOOTBALL_SIM_TEAM_AI_DECISION_FACTORY_HPP
