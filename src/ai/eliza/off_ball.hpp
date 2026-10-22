#ifndef FOOTBALL_AI_ELIZA_OFF_BALL_HPP
#define FOOTBALL_AI_ELIZA_OFF_BALL_HPP

#include "eliza_intent.hpp"
#include "eliza_parameters.hpp"
#include "football/foundation/math/vector3.hpp"
#include "football/observation/world_state.hpp"

namespace football::ai::eliza {

// Legacy AI_GetMindSet: role -> offensive mentality in [0, 1].
float MindSet(e_PlayerRole role);

// Static tactical anchor plus the observed ball shift, in pitch metres.
football::math::Vector3 CalculateFormationAnchor(
    const WorldState& world, const EffectivePlayerTactics* tactics,
    const WorldPlayerState& self);

// The off-ball movement target for one player: the formation anchor blended
// with the support force field and the defensive component by role and
// possession, kept onside and on the pitch.
football::math::Vector3 CalculateOffBallTarget(
    const WorldState& world, const EffectivePlayerTactics* tactics,
    const WorldPlayerState& self,
    const ElizaParameters& parameters, const ElizaIntent& intent = {});

}  // namespace football::ai::eliza

#endif  // FOOTBALL_AI_ELIZA_OFF_BALL_HPP
