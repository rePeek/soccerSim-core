#ifndef FOOTBALL_SIM_PLAYER_ACTION_MAPPING_HPP
#define FOOTBALL_SIM_PLAYER_ACTION_MAPPING_HPP

#include "football/animation/types.hpp"
#include "football/player/player_control.hpp"

namespace football::sim::player {

// A Command says what the player is asked to do. Animation::Match decides which
// clip expresses it. The mapping below is intentionally not one-to-one.
//
// ThroughPass is a pass into space, not a distinct animation: it resolves to a
// short or long pass from the explicit target distance. Beat resolves to the
// ball-control family, and standing tackle to Interfere rather than Sliding.
inline constexpr float kThroughPassLongDistance = 30.0f;

inline bool IsPassControlAction(ControlAction action) {
  return action == ControlAction::ShortPass ||
         action == ControlAction::LongPass ||
         action == ControlAction::ThroughPass ||
         action == ControlAction::HighPass;
}

// A pass must name its receiver or its target point. A shot must name its aim
// direction or target point. Neither may be inferred by the execution layer.
inline bool ControlActionRequiresExplicitTarget(ControlAction action) {
  return IsPassControlAction(action);
}

inline bool ControlActionRequiresAim(ControlAction action) {
  return action == ControlAction::Shoot;
}

inline football::math::Vector3 ResolveExplicitShotDirection(
    const PlayerControl& control, const football::math::Vector3& player_position,
    const football::math::Vector3& fallback_direction) {
  if (!control.target_position) return fallback_direction;
  return (*control.target_position - player_position)
      .GetNormalized(fallback_direction);
}

inline e_FunctionType ControlActionFunctionType(ControlAction action,
                                                float target_distance = 0.0f) {
  switch (action) {
    case ControlAction::ShortPass:  // Pass aliases this value.
      return e_FunctionType_ShortPass;
    case ControlAction::LongPass:
      return e_FunctionType_LongPass;
    case ControlAction::ThroughPass:
      return target_distance > kThroughPassLongDistance
                 ? e_FunctionType_LongPass
                 : e_FunctionType_ShortPass;
    case ControlAction::HighPass:
      return e_FunctionType_HighPass;
    case ControlAction::Shoot:
      return e_FunctionType_Shot;
    case ControlAction::Beat:
      return e_FunctionType_BallControl;
    case ControlAction::Tackle:
      return e_FunctionType_Interfere;
    case ControlAction::SlideTackle:
      return e_FunctionType_Sliding;
    case ControlAction::Pull:
    case ControlAction::CallSupport:
      // No execution clip exists. The caller rejects the Command rather than
      // substituting an unrelated animation or choosing a helper.
      return e_FunctionType_None;
    case ControlAction::Dribble:
      return e_FunctionType_BallControl;
    case ControlAction::Trap:
      return e_FunctionType_Trap;
    case ControlAction::Save:
      return e_FunctionType_Deflect;
    case ControlAction::None:
      return e_FunctionType_Movement;
  }
  return e_FunctionType_Movement;
}

}  // namespace football::sim::player

#endif  // FOOTBALL_SIM_PLAYER_ACTION_MAPPING_HPP
