// Copyright 2019 Google LLC & Bastiaan Konings
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "player/execution/kick_targeting.hpp"

#include <cassert>

#include "football/player/execution/player_pass_solver.hpp"
#include "player/player.hpp"

namespace football::sim::mechanics {

bool GetPass(Player* player, e_FunctionType passType, int defending_direction,
             Player* forcedTargetPlayer,
             const Vector3* forcedTargetPosition,
             Vector3& resultingDirection, float& resultingPower,
             Player*& targetPlayer) {
  assert(forcedTargetPlayer != player);
  // A value control may outlive the recipient's activation until contact.
  if (forcedTargetPlayer && !forcedTargetPlayer->IsActive())
    forcedTargetPlayer = nullptr;

  football::sim::player::PassSolverInput input;
  input.pass_type = passType;
  input.origin.position = player->GetPosition();
  input.origin.movement = player->GetMovement();
  input.origin.has_touch_position = player->TouchAnim();
  if (input.origin.has_touch_position)
    input.origin.touch_position = player->GetTouchPos();
  input.origin.defending_direction = defending_direction;
  if (forcedTargetPlayer) {
    input.target_player = football::sim::player::PassSolverTarget{
        forcedTargetPlayer->GetPosition(), forcedTargetPlayer->GetMovement()};
  }
  if (forcedTargetPosition != nullptr)
    input.target_position = *forcedTargetPosition;

  const football::sim::player::PassSolverResult solved =
      football::sim::player::SolvePass(input);
  if (!solved.solved) return false;

  resultingDirection = solved.direction;
  resultingPower = solved.power;
  targetPlayer = solved.targets_player ? forcedTargetPlayer : nullptr;
  return true;
}

}  // namespace football::sim::mechanics
