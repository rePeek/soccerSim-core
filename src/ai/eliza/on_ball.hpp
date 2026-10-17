#ifndef FOOTBALL_AI_ELIZA_ON_BALL_HPP
#define FOOTBALL_AI_ELIZA_ON_BALL_HPP

#include <optional>

#include "eliza_parameters.hpp"
#include "football/ai/tactical_board.hpp"
#include "football/foundation/math/vector3.hpp"
#include "football/observation/world_state.hpp"
#include "football/player/player_control.hpp"

namespace football::ai::eliza {

// One discrete intent inside the public PlayerControl vocabulary. The policy
// exposes a single best action; it never builds a command queue.
struct OnBallDecision {
  ControlAction action = ControlAction::None;
  std::optional<football::player::PlayerId> target_player;
  std::optional<football::math::Vector3> target_position;
  float power = 0.0f;
};

// Legacy _GetPassingOdds: 1 = safe, 0 = intercepted. `ball_velocity_multiplier`
// scales the ball travel time (the legacy shot uses 3.0).
float PassingOdds(const WorldState& world, const WorldPlayerState& self,
                  const football::math::Vector3& origin,
                  const football::math::Vector3& target, ControlAction pass_type,
                  float ball_velocity_multiplier);

// Choose the ball carrier's intent: shoot, pass (short/long/high) or keep
// dribbling (action None). The decision is deterministic; the legacy RNG draw
// is replaced by a fixed threshold.
OnBallDecision DecideOnBall(const WorldState& world, const TacticalBoard& board,
                            const WorldPlayerState& self,
                            const ElizaParameters& parameters);

// Movement toward the opponent goal while carrying the ball (legacy
// GetBestDribbleMovement).
football::math::Vector3 DribbleMovement(const WorldState& world,
                                        const WorldPlayerState& self,
                                        const ElizaParameters& parameters);

// Trap (fast ball) or Dribble (slow ball) for an actor reaching a loose ball.
OnBallDecision DecideLooseBall(const WorldState& world,
                               const WorldPlayerState& self,
                               const ElizaParameters& parameters);

// Tackle intent for a defender close to the opponent ball carrier.
ControlAction DecideTackle(const WorldState& world, const WorldPlayerState& self);

}  // namespace football::ai::eliza

#endif  // FOOTBALL_AI_ELIZA_ON_BALL_HPP
