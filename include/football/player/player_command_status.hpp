#ifndef FOOTBALL_SIM_PLAYER_COMMAND_STATUS_HPP
#define FOOTBALL_SIM_PLAYER_COMMAND_STATUS_HPP

#include <cstdint>

namespace football::sim::player {

// Why a Command produced (or failed to produce) an executable PlayerCommand.
// MovementOnly is continuous control, not a discrete action result.
enum class CommandBuildStatus : std::uint8_t {
  MovementOnly = 0,
  Accepted,
  // A discrete action was requested but the concrete target/ball/actor state
  // made it non-executable. The queue still carries the movement fallback.
  FallbackMovement,
  // The Command is malformed or not authorized (missing target/aim, rule gate).
  Rejected,
  // Defined Command with no execution path in the current build.
  Unsupported,
};

inline bool CommandBuildExecuted(CommandBuildStatus status) {
  return status == CommandBuildStatus::Accepted;
}

}  // namespace football::sim::player

#endif  // FOOTBALL_SIM_PLAYER_COMMAND_STATUS_HPP
