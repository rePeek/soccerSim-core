#include <cstdlib>
#include <type_traits>

#include "sim/player/player_control.hpp"
#include "sim/player/player_control_set.hpp"
#include "sim/observation/world_state.hpp"
#include "sim/time/tick.hpp"
#include "sim/time/tick_boundary.hpp"
#include "sim/simulation_config.hpp"

static_assert(football::sim::Minutes(45).value == 270000);
static_assert((football::sim::Tick{17} + football::sim::Seconds(2)).value == 217);
static_assert(football::sim::ToMilliseconds(football::sim::Tick{17}) == 170);
static_assert(std::is_same_v<decltype(MatchOptions::half_duration), football::sim::TickSpan>);
static_assert(std::is_same_v<decltype(WorldState::regulation_time), football::sim::TickSpan>);
static_assert(std::is_same_v<decltype(WorldState::ball_in_play_time), football::sim::TickSpan>);

int main() {
  if (MatchOptions{}.half_duration != football::sim::Minutes(45)) return EXIT_FAILURE;
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
