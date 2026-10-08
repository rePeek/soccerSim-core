#ifndef FOOTBALL_SIM_PLAYER_PLAYER_RUNTIME_SINK_HPP
#define FOOTBALL_SIM_PLAYER_PLAYER_RUNTIME_SINK_HPP

class Player;

namespace football::sim {
// Write-only runtime consequence published by Player/Humanoid execution.
// Actors decide contacts/retention; the composition root mutates world state.
class PlayerRuntimeSink {
 public:
  virtual ~PlayerRuntimeSink() = default;
  virtual void SetBallRetainer(Player* retainer) = 0;
};
}  // namespace football::sim

#endif