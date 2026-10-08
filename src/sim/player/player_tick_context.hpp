#ifndef FOOTBALL_SIM_PLAYER_TICK_CONTEXT_HPP
#define FOOTBALL_SIM_PLAYER_TICK_CONTEXT_HPP

#include "sim/time/tick.hpp"
#include "foundation/math/rng.hpp"
#include "football/ball/ball_environment.hpp"
#include "sim/event/touch_state.hpp"

class Player;
namespace football::ball { class Ball; }
class Team;
struct RefereeBuffer;
namespace football::model { class Pitch; }

namespace football::sim {
// Stack-local input for a single Player call. Never retained by an actor.
// Opening contact may start the half during that call: fatigue reads the live
// clock gate AFTER Humanoid execution, not a pre-contact frozen snapshot.
// play/set-piece/ball-in-play gates are also borrowed live because a synchronous
// touch notification can move the competition state inside the same Process().
struct PlayerTickContext {
  Tick now;
  const bool& play_authorized;
  const bool& set_piece_active;
  const bool& ball_in_play;
  const bool& half_underway;

  football::ball::Ball& ball;
  football::ball::BallEnvironment ball_environment;

  Player* ball_retainer;
  Player* designated_possession_player;
  const Player* last_touch_player;

  const event::TouchState& touches;
  const RefereeBuffer& restart;

  const football::model::Pitch& pitch;

  Team& own_team;
  Team& opponent_team;  // transitional: Player/Humanoid still read opponent records
  Team& first_processing_team;  // teams[FirstTeam()], preserving ApplyBallTouch order
  Team& second_processing_team;
  int processing_slot;  // own team's processing order (0 = first, 1 = second)

  bool restart_needs_simulation;

  blunted::Rng& rng;
};
} // namespace football::sim

#endif