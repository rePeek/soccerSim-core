#include <cstdlib>

#include "control/player_control_set.hpp"

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

  controls.Clear();
  return controls.Get(9) == nullptr ? EXIT_SUCCESS : EXIT_FAILURE;
}
