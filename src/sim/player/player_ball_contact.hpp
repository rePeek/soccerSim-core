#ifndef FOOTBALL_SIM_PLAYER_PLAYER_BALL_CONTACT_HPP
#define FOOTBALL_SIM_PLAYER_PLAYER_BALL_CONTACT_HPP

#include <optional>
#include <span>

#include "foundation/math/vector3.hpp"
#include "foundation/time/tick.hpp"
#include "sim/event/touch_state.hpp"

namespace football::ball { class Ball; }
class Player;
class Team;
class MentalImage;

namespace football::sim {

class SimulationFactSink;

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
  // Write-only touch port; never queried for world state.
  SimulationFactSink* touch_sink = nullptr;
  const event::TouchState& touches;
};

struct BallPlayerContactResult {
  std::optional<blunted::Vector3> impulse;
  float rotation_bias = 0.0f;
};

// Preserves supplied player order and per-volume accidental-touch notifications.
// Mutates player flags and publishes team/player touch facts through the explicit
// write-only sink; actors/Team expose no Match or Simulation lookup. football::ball::Ball impulse,
// dependent refresh, random rotation and cooldown publication belong to the
// calling runtime phase.
BallPlayerContactResult ResolveBallPlayerContacts(
    const football::ball::Ball& ball, std::span<Player* const> players,
    const BallPlayerContactInputs& inputs);

}  // namespace football::sim

#endif  // FOOTBALL_SIM_PLAYER_PLAYER_BALL_CONTACT_HPP