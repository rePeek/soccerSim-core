#ifndef FOOTBALL_SIM_PLAYER_PLAYER_RUNTIME_SINK_HPP
#define FOOTBALL_SIM_PLAYER_PLAYER_RUNTIME_SINK_HPP

class Player;

namespace football::sim {
// Write-only runtime consequence published by Player execution.
// Actors decide contacts/retention and designate a receiver; the composition
// root is the single writer of the runtime-owned world state.
class PlayerRuntimeSink {
 public:
  virtual ~PlayerRuntimeSink() = default;
  virtual void SetBallRetainer(Player* retainer) = 0;
  // A pass designates its receiver. Runtime still arbitrates possession at
  // tick boundaries; the team id is explicit so the port does not need the
  // actor once cross-module ids replace the raw pointer.
  virtual void SetDesignatedPlayer(int team_id, Player* receiver) = 0;
};
}  // namespace football::sim

#endif