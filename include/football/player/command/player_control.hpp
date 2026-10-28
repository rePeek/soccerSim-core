#ifndef FOOTBALL_SIM_PLAYER_CONTROL_HPP
#define FOOTBALL_SIM_PLAYER_CONTROL_HPP

#include <cstdint>
#include <optional>

#include "football/player/state/player_definition.hpp"
#include "football/foundation/math/vector3.hpp"

// One player's Command value for the next tick. Continuous movement is
// expressed by move_direction + desired_speed (there is no Move/Accelerate/
// Decelerate action verb); discrete actions use `action`.
//
// Discrete actions are attempts, not outcomes: Shot may miss, Beat may not
// beat anyone, and whether a pass arrives is decided by physics.
enum class ControlAction : std::uint8_t {
  None = 0,
  // Explicit short pass. `Pass` is the preferred spelling; ShortPass is kept
  // for source compatibility and has the same value.
  ShortPass = 1,
  Pass = ShortPass,
  LongPass,
  // Pass into a specified space. The execution layer chooses the concrete
  // short/long/high pass animation from the target distance and ball state.
  ThroughPass,
  // Legacy explicit lofted pass. Kept as an execution primitive.
  HighPass,
  // Explicit shot. Requires target_position or a non-zero move_direction.
  Shoot,
  // Attempted escape move (change of direction / feint / knock-on).
  Beat,
  // Standing tackle / poke. `SlideTackle` is the sliding challenge.
  Tackle,
  SlideTackle,
  // Pull/hold an opponent. Defined as a command, but unsupported by the
  // current animation assets: the execution layer rejects it rather than
  // substituting an unrelated clip.
  Pull,
  // Request support from a teammate. Team/Tactical Planner owns which teammate
  // and where; Player reports Unsupported and never selects a helper itself.
  CallSupport,
  // Legacy execution primitives. Prefer Beat for an escape attempt and the
  // pass verbs above for distribution.
  Dribble,
  Trap,
  Save,
};

// Executable request for one simulation tick, independent of its input source.
// A caller may retain/resubmit it; simulation translates it into commands.
struct PlayerControl {
  football::player::PlayerId player = football::player::kInvalidPlayerId;
  football::math::Vector3 move_direction = football::math::Vector3(0);
  float desired_speed = 0.0f;
  std::optional<football::math::Vector3> look_at;
  ControlAction action = ControlAction::None;
  std::optional<football::player::PlayerId> target_player;
  std::optional<football::math::Vector3> target_position;
  float power = 0.0f;
};

#endif  // FOOTBALL_SIM_PLAYER_CONTROL_HPP
