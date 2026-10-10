#ifndef FOOTBALL_SIM_REFEREE_BALL_RULES_HPP
#define FOOTBALL_SIM_REFEREE_BALL_RULES_HPP

#include <optional>

#include "foundation/math/vector3.hpp"
#include "model/pitch.hpp"

namespace football::sim {

// Pure ball-state predicates over observed positions. These are the query
// surface the referee will use instead of the synchronous BallBoundaryFact
// stream: the referee classifies the out-of-play condition from the snapshot
// it is settling, not from a fact handed to it mid-tick.
enum class BallOutOfPlay {
  None,
  Touchline,
  GoalLine,
};

// Geometry only, in one shared frame. The caller supplies the legacy
// goal-scored authorization separately.
BallOutOfPlay ClassifyBallOutOfPlay(const football::model::Pitch& pitch,
                                    const blunted::Vector3& ball_position);

// The +1/-1 goal side whose goal line the ball crossed between two consecutive
// observed positions, or nullopt. Reuses the legacy CrossedGoalLine segment
// endpoints, bidirectional crossing and side-net veto.
std::optional<int> CrossedGoalMouth(const football::model::Pitch& pitch,
                                    const blunted::Vector3& previous,
                                    const blunted::Vector3& current);

}  // namespace football::sim

#endif  // FOOTBALL_SIM_REFEREE_BALL_RULES_HPP
