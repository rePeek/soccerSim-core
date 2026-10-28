#ifndef FOOTBALL_SIM_PLAYER_PLAYER_BALL_CONTACT_HPP
#define FOOTBALL_SIM_PLAYER_PLAYER_BALL_CONTACT_HPP

#include <optional>
#include <span>

#include "football/foundation/math/vector3.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/player/player_touch_facts.hpp"
#include "player/contact/player_touch_event.hpp"
#include "player/state/player_observation.hpp"

namespace football::ball { class Ball; }
class Player;

#include "player/execution/player_team_view.hpp"

namespace football::sim {


// Borrowed, tick-local dependencies; neither Match nor Simulation is a service
// locator for the resolver. All actors/ball/history must share one physical frame.
struct BallPlayerContactInputs {
  std::span<const PlayerTeamView, 2> teams;
  int fallback_last_touch_team;
  // Initial latest-touch fact; the sweep advances its local cursor on each hit.
  int last_touch_team;
  const football::sim::player::PlayerObservation& observation;
  Tick now;
  Tick last_body_collision;
  // Write-only touch port; never queried for world state.
  football::sim::player::PlayerTouchSink* touch_sink = nullptr;
  const football::player::PlayerTouchFacts& touches;
};

struct BallPlayerContactResult {
  std::optional<football::math::Vector3> impulse;
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