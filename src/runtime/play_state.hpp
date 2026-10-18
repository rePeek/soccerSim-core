#ifndef FOOTBALL_SIM_RUNTIME_PLAY_STATE_HPP
#define FOOTBALL_SIM_RUNTIME_PLAY_STATE_HPP

namespace football::sim {

// Runtime-owned classification of match control. It is *derived* from the
// authorization flags and the restart context, so it is never a second
// authority: it only names the execution state the runtime is already in and
// lets callers describe stopped-play behaviour without re-deriving it.
//
// Playing       normal football; every legal PlayerControl is executable
// Stopped       play is stopped and no restart is being organized yet
// RestartSetup  a restart is being organized; actors may keep moving
// RestartReady  the restart is authorized; only its taker may act
enum class PlayState {
  Playing,
  Stopped,
  RestartSetup,
  RestartReady,
};

}  // namespace football::sim

#endif  // FOOTBALL_SIM_RUNTIME_PLAY_STATE_HPP
