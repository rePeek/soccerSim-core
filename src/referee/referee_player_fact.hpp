#ifndef FOOTBALL_SIM_RULES_REFEREE_PLAYER_FACT_HPP
#define FOOTBALL_SIM_RULES_REFEREE_PLAYER_FACT_HPP

#include "football/foundation/math/vector3.hpp"
#include "football/player/player_definition.hpp"

namespace football::sim::rules {

// One actor's read-only rule facts for offside evaluation, sampled this instant
// in the processing frame. No Player object, no observation history and no
// animation/action state: the rules only need identity, team and position.
struct RefereePlayerFact {
  football::player::PlayerId id = football::player::kInvalidPlayerId;
  int team_id = -1;
  football::math::Vector3 position;
};

}  // namespace football::sim::rules

#endif  // FOOTBALL_SIM_RULES_REFEREE_PLAYER_FACT_HPP
