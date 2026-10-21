#ifndef FOOTBALL_SIM_PLAYER_PLAYER_SEND_OFF_SINK_HPP
#define FOOTBALL_SIM_PLAYER_PLAYER_SEND_OFF_SINK_HPP

// Player is a global sim actor type (see player/player.hpp); declare it
// outside the namespace so this header never introduces football::sim::Player.
class Player;

namespace football::sim {

// Write-only port for leaving the pitch. Player only deactivates itself and
// reports the event; Runtime owns the team-level follow-up (formation roles,
// possession re-designation, events). Keeping the trigger synchronous at the
// same per-actor point preserves the legacy execution order.
class PlayerSendOffSink {
 public:
  virtual ~PlayerSendOffSink() = default;
  virtual void OnPlayerSentOff(Player& player) = 0;
};

}  // namespace football::sim

#endif  // FOOTBALL_SIM_PLAYER_PLAYER_SEND_OFF_SINK_HPP
