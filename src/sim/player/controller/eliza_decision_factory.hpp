#ifndef FOOTBALL_SIM_ELIZA_DECISION_FACTORY_HPP
#define FOOTBALL_SIM_ELIZA_DECISION_FACTORY_HPP

#include <memory>

#include "sim/player/controller/legacy_player_decision_factory.hpp"

// TRANSITIONAL. Today's default player decision, defined beside the
// implementation it creates. It moves with ElizaController into src/ai, and the
// composition root will inject it instead of Simulation constructing a default.
std::shared_ptr<const LegacyPlayerDecisionFactory>
MakeElizaPlayerDecisionFactory();

#endif  // FOOTBALL_SIM_ELIZA_DECISION_FACTORY_HPP
