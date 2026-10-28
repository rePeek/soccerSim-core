#include "football/player/player_control.hpp"
#include "football/player/player_control_set.hpp"
#include "football/player/player_action_request.hpp"
#include "football/player/player_command_validation.hpp"

int main() {
  PlayerControl control;
  control.action = ControlAction::ThroughPass;
  if (football::sim::player::ControlActionTargetIsExplicit(control)) return 1;
  control.target_position = football::math::Vector3(10, 0, 0);
  if (!football::sim::player::ControlActionTargetIsExplicit(control)) return 2;
  PlayerControlSet controls;
  controls.Set(7, control);
  const auto* found = controls.Get(7);
  if (!found || found->player != 7 || found->action != control.action) return 3;
  control.action = ControlAction::Shoot;
  control.target_position.reset();
  if (football::sim::player::ControlActionAimIsExplicit(control)) return 4;
  PlayerActionRequest request;
  request.action = ControlAction::Shoot;
  PlayerReaction reaction;
  return request.action == ControlAction::Shoot && reaction.kind == ReactionKind::None ? 0 : 5;
}
