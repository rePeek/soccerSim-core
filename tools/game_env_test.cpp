#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <type_traits>

#include "app/fixtures/default_teams.hpp"
#include "env/game_env.hpp"
#include "sim/simulation.hpp"
#include "../test/default_ai_fixture.hpp"

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
template<class T> concept HasRuntimeAccess =
    requires(T& env) { env.simulation(); } || requires(T& env) { env.simulation_; } ||
    requires(T& env) { env.GetSimulation(); } || requires(T& env) { env.match(); } ||
    requires { &T::GetContext; };
static_assert(!HasRuntimeAccess<GameEnv>);
template<class T> concept HasConcretePolicyAccess = requires(T& env) { env.default_ai(); };
static_assert(!HasConcretePolicyAccess<GameEnv>);

namespace {


void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

GameEnv MakeGame() {
  return GameEnv{football::app::fixtures::MakeDefaultHomeTeam(),
                 football::app::fixtures::MakeDefaultAwayTeam(),
                 football::model::MakeLegacyPitch()};
}

bool SameVector(const blunted::Vector3& a, const blunted::Vector3& b) {
  return std::memcmp(a.coords, b.coords, sizeof(a.coords)) == 0;
}

void RequireSameWorld(const WorldState& a, const WorldState& b) {
  Require(a.tick == b.tick && a.reset_sequence == b.reset_sequence &&
              SameVector(a.ball_position, b.ball_position) &&
              SameVector(a.ball_velocity, b.ball_velocity) &&
              a.in_play == b.in_play && a.in_set_piece == b.in_set_piece &&
              a.restart == b.restart && a.restart_taker == b.restart_taker &&
              a.ball_retainer == b.ball_retainer && a.players.size() == b.players.size(),
          "world snapshot mismatch");
  for (std::size_t i = 0; i < a.players.size(); ++i) {
    const auto& x = a.players[i];
    const auto& y = b.players[i];
    Require(x.id == y.id && x.side == y.side && x.active == y.active &&
                x.has_possession == y.has_possession &&
                x.lazy == y.lazy && x.max_speed == y.max_speed &&
                SameVector(x.position, y.position) &&
                SameVector(x.velocity, y.velocity) && SameVector(x.facing, y.facing),
            "player snapshot mismatch");
  }
}

void CheckCoreAPI() {
  auto game = MakeGame();
  game.controls().Set(0, PlayerControl{});
  game.stop_game();  // Safe before startup, and safe repeatedly.
  Require(game.controls().controls().empty(), "inactive stop retained controls");
  game.stop_game();
  game.controls().Set(0, PlayerControl{});
  game.start_game();
  Require(game.controls().controls().empty(), "startup retained controls");
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

  game.controls().Set(initial.players.front().id, PlayerControl{});
  game.stop_game();
  Require(game.controls().controls().empty(), "stop retained old controls");
  game.stop_game();
  game.start_game();
  RequireSameWorld(game.observe(), initial);
  for (int i = 0; i < 100; ++i) game.step();
  RequireSameWorld(game.observe(), first_run);

  // A second live environment must not interfere with the first one's runtime.
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
  game.step();  // Destroying another environment must preserve the surviving one.
  Require(game.observe().tick == 101, "peer teardown damaged live game");
}

void CheckDeclaredIdentity() {
  namespace model = football::model;
  auto home = football::app::fixtures::MakeDefaultHomeTeam();
  auto away = football::app::fixtures::MakeDefaultAwayTeam();
  home.players[0].id = model::kInvalidPlayerId - 1;
  away.players[0].id = 0;
  GameEnv game{home, away, model::MakeLegacyPitch()};
  home.players[0].id = 100;  // Environment keeps its own descriptions.
  game.start_game();
  const WorldState initial = game.observe();
  Require(initial.players[0].id == model::kInvalidPlayerId - 1 &&
              initial.players[0].side == model::TeamSide::Home &&
              initial.players[11].id == 0 &&
              initial.players[11].side == model::TeamSide::Away,
          "GameEnv replaced declared identities/sides with ordinal IDs");
  game.controls().Set(initial.players[11].id, PlayerControl{});
  game.step();
  game.reset_game();
  RequireSameWorld(game.observe(), initial);
  game.stop_game();
  game.start_game();
  RequireSameWorld(game.observe(), initial);
}

void CheckIndependentLifetimes() {
  auto reference = MakeGame();
  auto survivor = MakeGame();
  reference.start_game();
  survivor.start_game();
  PlayerControl command;
  command.move_direction = blunted::Vector3(1, 0, 0);
  command.desired_speed = 5.0f;
  reference.controls().Set(1, command);
  survivor.controls().Set(1, command);
  {
    auto peer = MakeGame();
    peer.start_game();
    command.move_direction = blunted::Vector3(0, -1, 0);
    peer.controls().Set(12, command);
    for (int tick = 0; tick < 150; ++tick) {
      reference.step();
      survivor.step();
      if (tick == 40) {
        peer.reset_game();
        Require(peer.controls().controls().empty(), "peer reset retained controls");
        peer.controls().Set(12, command);
      }
      if (tick == 80) {
        peer.stop_game();
        Require(peer.controls().controls().empty(), "peer stop retained controls");
      }
      if (tick == 81) peer.start_game();
      if (tick % 3 == 0) peer.step();  // Deliberately different clocks.
      Require(survivor.controls().Get(1) != nullptr, "peer cleared surviving controls");
      RequireSameWorld(survivor.observe(), reference.observe());
    }
    peer.step();
  }  // Destroy the last stepped environment while the other two remain live.
  survivor.step();
  reference.step();
  RequireSameWorld(survivor.observe(), reference.observe());
}

void CheckDeclaredComposition() {
  namespace model = football::model;
  auto home = football::app::fixtures::MakeDefaultHomeTeam();
  const auto away = football::app::fixtures::MakeDefaultAwayTeam();
  home.name = "Static Home";
  home.players[1].attributes.set(model::PlayerStat::physical_velocity, 0.8123456f);
  home.players[1].height = 1.93f;
  const auto declared_home = home;
  GameEnv game{home, away, model::MakeLegacyPitch()};
  home.name = "Changed after construction";
  home.players[1].attributes.fill(0.1f);
  home.players[1].height = 1.5f;
  game.start_game();
  auto reference = std::make_unique<Simulation>();
  reference->Init(declared_home, away, model::MakeLegacyPitch(), MatchOptions{}, false);
  auto policy = football::test::MakeDefaultAI(*reference);
  for (int repeat = 0; repeat < 3; ++repeat) {
    RequireSameWorld(game.observe(), reference->Observe());
    for (int tick = 0; tick < 600; ++tick) {
      game.step();
      football::test::StepDefaultAI(*reference, policy);
      RequireSameWorld(game.observe(), reference->Observe());
    }
    if (repeat == 0) {
      game.reset_game();
      reference->Stop();
      reference->Init(declared_home, away, model::MakeLegacyPitch(), MatchOptions{}, false);
    } else if (repeat == 1) {
      game.stop_game();
      game.start_game();
      reference = std::make_unique<Simulation>();
      reference->Init(declared_home, away, model::MakeLegacyPitch(), MatchOptions{}, false);
    }
  }
}

void CheckRejectedStartup() {
  auto live = MakeGame();
  live.start_game();
  const WorldState initial = live.observe();
  auto home = football::app::fixtures::MakeDefaultHomeTeam();
  auto away = football::app::fixtures::MakeDefaultAwayTeam();
  away.players.front().id = home.players.front().id;
  GameEnv rejected{home, away, football::model::MakeLegacyPitch()};
  // Failed startup must not publish a half-initialized Simulation. Repeating
  // startup without stop should throw validation again, not a lifecycle assert.
  for (int attempt = 0; attempt < 2; ++attempt) {
    rejected.controls().Set(0, PlayerControl{});
    bool threw = false;
    try {
      rejected.start_game();
    } catch (const std::invalid_argument&) {
      threw = true;
    }
    Require(threw && rejected.controls().controls().empty(),
            "rejected startup retained a runtime or stale controls");
    RequireSameWorld(live.observe(), initial);
  }
  rejected.stop_game();
  rejected.stop_game();
  live.step();
  Require(live.observe().tick == 1, "rejected startup damaged the live environment");
}

}  // namespace

int main() {
  try {
    CheckCoreAPI();
    CheckDeclaredIdentity();
    CheckIndependentLifetimes();
    CheckDeclaredComposition();
    CheckRejectedStartup();
    std::cout << "football_game_env_test: PASS\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "football_game_env_test: FAIL: " << error.what() << '\n';
    return 1;
  }
}
