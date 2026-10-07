#ifndef FOOTBALL_SIM_BALL_TOUCH_APPLICATION_HPP
#define FOOTBALL_SIM_BALL_TOUCH_APPLICATION_HPP

#include <span>
#include "foundation/math/vector3.hpp"
#include "sim/ball/ball_environment.hpp"

class Ball;
class MentalImage;
class Team;

namespace football::sim {
// Synchronous legacy composition, not physics, touch accounting or an event queue.
// Caller supplies the current physical frame and processing-roster order; rotation
// and last-touch/rule publication remain at their original caller mutation points.
void ApplyBallTouch(Ball& ball, BallEnvironment environment, const blunted::Vector3& impulse,
                    std::span<MentalImage> history, Team& first, Team& second);
}  // namespace football::sim
#endif
