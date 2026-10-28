#include "player/state/player_action.hpp"
#include "player/state/player_kinematics.hpp"

int main() {
  PlayerKinematicState state;
  PlayerActionState action;
  return state.position.GetLength() == 0.0f && action.type == e_FunctionType_None
             ? 0
             : 1;
}
