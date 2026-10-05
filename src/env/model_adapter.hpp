#ifndef FOOTBALL_ENV_MODEL_ADAPTER_HPP
#define FOOTBALL_ENV_MODEL_ADAPTER_HPP

#include "model/team.hpp"

struct ScenarioConfig;

// Keep the caller's episode description in public coordinates. The environment
// retains a converted copy with effective initial formations for legacy readers.
ScenarioConfig ToRuntimeScenario(const ScenarioConfig& scenario,
                                 const football::model::Team& home,
                                 const football::model::Team& away);

#endif  // FOOTBALL_ENV_MODEL_ADAPTER_HPP
