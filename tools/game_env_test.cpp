#include <cstring>
#include <iostream>
#include <stdexcept>
#include <type_traits>

#include "data/default_teams.hpp"
#include "env/game_env.hpp"

static_assert(!std::is_default_constructible_v<GameEnv>);
static_assert(!std::is_copy_constructible_v<GameEnv>);
static_assert(!std::is_move_constructible_v<GameEnv>);

template<class T> concept HasLegacyObservation = requires(T& env) { env.get_info(); };
template<class T> concept HasLegacyCheckpoint =
    requires { &T::get_state; } || requires { &T::set_state; } ||
    requires { &T::ProcessState; };
template<class T> concept HasLegacyInit = requires { &T::init; };
template<class T> concept HasLegacyReset = requires { &T::reset; };
template<class T> concept HasLegacyFields =
    requires(T& env) { env.context; } || requires(T& env) { env.context_; } ||
    requires(T& env) { env.scenario_config; } ||
    requires(T& env) { env.scenario_config_; } ||
    requires(T& env) { env.physics_steps_per_frame; } ||
    requires { &T::controls_; } || requires(T& env) { env.state; } ||
    requires(T& env) { env.waiting_for_game_count; };
static_assert(!HasLegacyObservation<GameEnv>);
static_assert(!HasLegacyCheckpoint<GameEnv>);
static_assert(!HasLegacyInit<GameEnv>);
static_assert(!HasLegacyReset<GameEnv>);
static_assert(!HasLegacyFields<GameEnv>);

namespace {

void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

GameEnv MakeGame() {
  return GameEnv{football::data::MakeDefaultHomeTeam(),
                 football::data::MakeDefaultAwayTeam(),
                 football::model::MakeLegacyPitch()};
}

bool SameVector(const blunted::Vector3& a, const blunted::Vector3& b) {
  return std::memcmp(a.coords, b.coords, sizeof(a.coords)) == 0;
}

void RequireSameWorld(const WorldState& a, const WorldState& b) {
  Require(a.tick == b.tick && SameVector(a.ball_position, b.ball_position) &&
              a.players.size() == b.players.size(),
          "world snapshot mismatch");
  for (std::size_t i = 0; i < a.players.size(); ++i) {
    const auto& x = a.players[i];
    const auto& y = b.players[i];
    Require(x.id == y.id && x.team == y.team && x.active == y.active &&
                x.has_possession == y.has_possession &&
                SameVector(x.position, y.position) &&
                SameVector(x.velocity, y.velocity) && SameVector(x.facing, y.facing),
            "player snapshot mismatch");
  }
}

void CheckCoreAPI() {
  auto game = MakeGame();
  game.stop_game();  // Safe before startup, and safe repeatedly.
  game.stop_game();
  game.start_game();
  const WorldState initial = game.observe();
  Require(initial.tick == 0 && initial.players.size() == 22, "invalid startup");
  game.step();
  Require(game.observe().tick == 1, "step must advance one 10 ms tick");
  Require(initial.tick == 0, "retained observation was mutated");
  for (int i = 1; i < 100; ++i) game.step();
  const WorldState first_run = game.observe();
  Require(first_run.tick == 100, "100 calls must mean 100 simulation ticks");

  game.controls().Set(initial.players.front().id, PlayerControl{});
  game.reset_game();
  Require(game.controls().controls().empty(), "reset retained old controls");
  RequireSameWorld(game.observe(), initial);
  for (int i = 0; i < 100; ++i) game.step();
  RequireSameWorld(game.observe(), first_run);

  game.stop_game();
  game.stop_game();
  game.start_game();
  RequireSameWorld(game.observe(), initial);
  for (int i = 0; i < 100; ++i) game.step();
  RequireSameWorld(game.observe(), first_run);

  // A second live environment must not hijack the first one's legacy globals.
  {
    auto other = MakeGame();
    other.start_game();
    game.reset_game();
    for (int i = 0; i < 100; ++i) {
      other.step();
      game.step();
    }
    RequireSameWorld(other.observe(), first_run);
    RequireSameWorld(game.observe(), first_run);
  }
  game.step();  // Inactive environment destruction must preserve the live one.
  Require(game.observe().tick == 101, "inactive teardown damaged live game");
}

}  // namespace

int main() {
  try {
    CheckCoreAPI();
    std::cout << "football_game_env_test: PASS\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "football_game_env_test: FAIL: " << error.what() << '\n';
    return 1;
  }
}
