#ifndef FOOTBALL_SIM_REFEREE_RESTART_PHASE_HPP
#define FOOTBALL_SIM_REFEREE_RESTART_PHASE_HPP

namespace football::sim {

// Restart execution lifecycle. Pending = still being set up; Ready = the rules
// authorize the taker to release; Taken = a legal release was accepted; InPlay
// is the terminal value once the context stops owning play. Declared by the
// rules so Runtime-owned restart state can expose it without the Referee
// depending on Runtime's private headers.
enum class RestartPhase { Pending, Ready, Taken, InPlay };

}  // namespace football::sim

#endif  // FOOTBALL_SIM_REFEREE_RESTART_PHASE_HPP
