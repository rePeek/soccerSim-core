#ifndef FOOTBALL_SIM_BALL_BALL_PLAYER_CONTACT_HPP
#define FOOTBALL_SIM_BALL_BALL_PLAYER_CONTACT_HPP

#include <optional>
#include <span>

#include "foundation/math/vector3.hpp"
#include "sim/time/tick.hpp"

class Ball;
class Player;
class Team;
class MentalImage;

namespace football::sim {

// Borrowed, tick-local dependencies; neither Match nor Simulation is a service
// locator for the resolver. All actors/ball/history must share one physical frame.
struct BallPlayerContactInputs {
  std::span<Team* const, 2> teams;
  int fallback_last_touch_team;
  // Initial latest-touch fact; the sweep advances its local cursor on each hit.
  int last_touch_team;
  std::span<const MentalImage> history;
  Tick now;
  Tick last_body_collision;
};

struct BallPlayerContactResult {
  std::optional<blunted::Vector3> impulse;
  float rotation_bias = 0.0f;
};

// Preserves supplied player order and per-volume accidental-touch notifications.
// Mutates player flags and Team touch records through their existing APIs; these
// legacy actor/rule bridges are not yet decoupled. Ball impulse, dependent refresh,
// random rotation and cooldown publication belong to the calling runtime phase.
BallPlayerContactResult ResolveBallPlayerContacts(
    const Ball& ball, std::span<Player* const> players,
    const BallPlayerContactInputs& inputs);

}  // namespace football::sim

#endif  // FOOTBALL_SIM_BALL_BALL_PLAYER_CONTACT_HPP
