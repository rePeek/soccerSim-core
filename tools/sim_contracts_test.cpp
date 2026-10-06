#include <cstdlib>

#include "sim/player_control.hpp"
#include "sim/player_control_set.hpp"
#include "sim/world_state.hpp"

int main() {
  PlayerControlSet controls;
  PlayerControl control;
  control.desired_speed = 0.75f;
  controls.Set(9, control);

  if (controls.controls().size() != 1 || controls.Get(9) == nullptr ||
      controls.Get(9)->player != 9 || controls.Get(9)->desired_speed != 0.75f) {
    return EXIT_FAILURE;
  }

  control.desired_speed = 1.0f;
  controls.Set(9, control);
  if (controls.controls().size() != 1 || controls.Get(9)->desired_speed != 1.0f) {
    return EXIT_FAILURE;
  }

  const auto retained_controls = controls;
  WorldState world;
  world.players.push_back(WorldPlayerState{});
  world.players[0].id = 9;
  const auto retained_world = world;
  world.players[0].id = 17;
  world.players.clear();
  if (retained_world.players.size() != 1 || retained_world.players[0].id != 9 ||
      retained_controls.Get(9)->desired_speed != 1.f) return EXIT_FAILURE;

  controls.Clear();
  return controls.Get(9) == nullptr ? EXIT_SUCCESS : EXIT_FAILURE;
}
