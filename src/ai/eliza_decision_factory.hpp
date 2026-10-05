#ifndef FOOTBALL_AI_ELIZA_DECISION_FACTORY_HPP
#define FOOTBALL_AI_ELIZA_DECISION_FACTORY_HPP

#include <memory>

#include "sim/legacy_player_decision_factory.hpp"

// The default player decision implementation, chosen by the composition root
// (today src/env/game_env.cpp). Simulation never constructs it.
std::shared_ptr<const LegacyPlayerDecisionFactory>
CreateDefaultElizaDecisionFactory();

#endif  // FOOTBALL_AI_ELIZA_DECISION_FACTORY_HPP
