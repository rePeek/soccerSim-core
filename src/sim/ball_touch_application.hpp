#ifndef FOOTBALL_SIM_BALL_TOUCH_APPLICATION_HPP
#define FOOTBALL_SIM_BALL_TOUCH_APPLICATION_HPP

#include <span>
#include "foundation/math/vector3.hpp"
#include "football/ball/ball_environment.hpp"
#include "foundation/time/tick.hpp"

namespace football::ball { class Ball; }
class MentalImage;
class Team;
class Player;

namespace football::sim {
// Synchronous legacy composition, not physics, touch accounting or an event queue.
// Caller supplies the current physical frame and processing-roster order; rotation
// and last-touch/rule publication remain at their original caller mutation points.
void ApplyBallTouch(football::ball::Ball& ball, football::ball::BallEnvironment environment,
                    const blunted::Vector3& impulse,
                    std::span<MentalImage> history, Team& first, Team& second,
                    Tick now, const Player* retainer);

}  // namespace football::sim

#endif  // FOOTBALL_SIM_BALL_TOUCH_APPLICATION_HPP