#ifndef FOOTBALL_SIM_PLAYER_TICK_CONTEXT_HPP
#define FOOTBALL_SIM_PLAYER_TICK_CONTEXT_HPP

#include "football/foundation/time/tick.hpp"
#include "football/pitch/pitch.hpp"
#include "football/foundation/math/rng.hpp"
#include "football/player/player_touch_facts.hpp"
#include "player/player_authorization.hpp"
#include "player/player_team_view.hpp"
#include "player/player_touch_preparation.hpp"
#include "football/player/player_restart_facts.hpp"

class Player;
namespace football::ball { class Ball; }
class ActiveTouchShadowSink;
namespace football::pitch { class Pitch; }

namespace football::sim {
// Write-only send-off port implemented by Runtime (declared inside the
// namespace, unlike the global diagnostic sink above).
class PlayerSendOffSink;
// Stack-local input for a single Player call. Never retained by an actor.
// Opening contact may start the half during that call: fatigue reads the live
// clock gate AFTER the action tick, not a pre-contact frozen snapshot.
// play/set-piece/ball-in-play gates are also borrowed live because a synchronous
// touch notification can move the competition state inside the same Process().
struct PlayerTickContext {
  Tick now;
  const bool& play_authorized;
  const bool& set_piece_active;
  const bool& ball_in_play;
  const bool& half_underway;

  const football::ball::Ball& ball;

  Player* ball_retainer;
  Player* designated_possession_player;
  const Player* last_touch_player;

  const football::player::PlayerTouchFacts& touches;
  const football::player::PlayerRestartFacts& restart;

  const football::pitch::Pitch& pitch;

  // Borrowed team views: roster slices + runtime-owned match facts. Not a Team
  // entity and never retained beyond this call.
  PlayerTeamView own_team;
  PlayerTeamView opponent_team;
  int processing_slot;  // own team's processing order (0 = first, 1 = second)

  bool restart_needs_simulation;

  football::math::Rng& rng;
  ActiveTouchShadowSink* active_touch_shadow = nullptr; // optional read-only diagnostic
  // Non-null only in the prepare phase. Ball is an immutable endpoint view:
  // touches/rotation/retention are proposals, not immediate mutations/events.
  PlayerTouchPreparationSink* touch_preparation = nullptr;
  football::ball::Ball* legacy_ball = nullptr;
  // Write-only port: a send-off reports itself and Runtime performs the
  // team-level follow-up. Optional; a null port only skips that follow-up.
  PlayerSendOffSink* send_off = nullptr;
  // Runtime-owned rule adapter. Non-null in a match; Player consumes only the
  // returned value and never names a Referee type.
  const football::sim::player::PlayerAuthorizationPort* authorization = nullptr;
};
} // namespace football::sim

#endif
