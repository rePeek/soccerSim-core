#include <cstdlib>

#include "sim/player_control.hpp"
#include "sim/player_control_set.hpp"
#include "sim/world_state.hpp"
#include "sim/tick.hpp"
#include "sim/tick_boundary.hpp"

static_assert(football::sim::Minutes(45).value == 270000);
static_assert((football::sim::Tick{17} + football::sim::Seconds(2)).value == 217);
static_assert(football::sim::ToMilliseconds(football::sim::Tick{17}) == 170);

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
  if (world.simulation_epoch.valid()) return EXIT_FAILURE;
  world.simulation_epoch = ObservationEpoch::New();
  world.players.push_back(WorldPlayerState{});
  world.players[0].id = 9;
  const auto retained_world = world;
  world.players[0].id = 17;
  world.players.clear();
  if (retained_world.players.size() != 1 || retained_world.players[0].id != 9 ||
      retained_controls.Get(9)->desired_speed != 1.f) return EXIT_FAILURE;
  if (!retained_world.simulation_epoch.valid() ||
      retained_world.simulation_epoch != world.simulation_epoch) return EXIT_FAILURE;
  world.simulation_epoch = ObservationEpoch::New();
  if (retained_world.simulation_epoch == world.simulation_epoch) return EXIT_FAILURE;

  controls.Clear();
  return controls.Get(9) == nullptr ? EXIT_SUCCESS : EXIT_FAILURE;
}
