#ifndef FOOTBALL_SIM_REFEREE_BALL_RULES_HPP
#define FOOTBALL_SIM_REFEREE_BALL_RULES_HPP

#include <optional>

#include "football/foundation/math/vector3.hpp"
#include "football/pitch/pitch.hpp"

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
// goal-scored authorization separately. `ball_radius` is the physical sphere
// radius; a boundary is only crossed when the whole ball is past it.
BallOutOfPlay ClassifyBallOutOfPlay(const football::pitch::Pitch& pitch,
                                    const football::math::Vector3& ball_position,
                                    float ball_radius);

std::optional<football::math::Vector3> BoundaryCrossingPoint(
    const football::pitch::Pitch& pitch,
    const football::math::Vector3& previous,
    const football::math::Vector3& current, float ball_radius,
    BallOutOfPlay boundary);

// The first boundary the whole ball crosses between two observed positions.
// The goal-line and touchline crossings are compared by their step fraction,
// so a diagonal exit is classified by the line it actually crossed first and
// the reported position is that exact crossing point.
struct BallBoundaryCrossing {
  BallOutOfPlay boundary = BallOutOfPlay::None;
  football::math::Vector3 position;
  float fraction = 0.0f;
};
BallBoundaryCrossing DetectBallBoundaryCrossing(
    const football::pitch::Pitch& pitch,
    const football::math::Vector3& previous,
    const football::math::Vector3& current, float ball_radius);

}  // namespace football::sim

#endif  // FOOTBALL_SIM_REFEREE_BALL_RULES_HPP
