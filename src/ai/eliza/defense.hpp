#ifndef FOOTBALL_AI_ELIZA_DEFENSE_HPP
#define FOOTBALL_AI_ELIZA_DEFENSE_HPP

#include "football/ai/tactical_board.hpp"
#include "football/foundation/math/vector3.hpp"
#include "football/observation/world_state.hpp"

namespace football::ai::eliza {

// The opponent this player should defend: the board's marking target when set
// and active, else the opponent nearest the ball.
const WorldPlayerState* SelectMarkingTarget(const WorldState& world,
                                            const WorldPlayerState& self,
                                            const PlayerDirective* directive);

// Legacy GetDefendPosition: the point on the opponent -> own-goal line that is
// as close to us as to the opponent.
football::math::Vector3 CalculateDefendPosition(
    const WorldState& world, const WorldPlayerState& self,
    const WorldPlayerState& opponent);

// Legacy AddDefensiveComponent: bias the desired position toward a spot that
// stays closer to the opponent's shooting point than the opponent does.
football::math::Vector3 ApplyDefensiveComponent(
    const WorldState& world, const WorldPlayerState& self,
    const WorldPlayerState& opponent, const football::math::Vector3& desired,
    float bias);

}  // namespace football::ai::eliza

#endif  // FOOTBALL_AI_ELIZA_DEFENSE_HPP
