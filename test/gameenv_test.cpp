#include <cstring>
#include <iostream>
#include <stdexcept>
#include <type_traits>

#include "app/fixtures/default_teams.hpp"
#include "gameenv.hpp"
#include "default_ai_fixture.hpp"

static_assert(!std::is_default_constructible_v<GameEnv>);
static_assert(!std::is_copy_constructible_v<GameEnv>);
static_assert(!std::is_move_constructible_v<GameEnv>);
template<class T> concept HasInteractiveAPI =
    requires { &T::controls; } || requires { &T::tactics; } ||
    requires { &T::request_attacking_run; } || requires { &T::request_team_pressure; } ||
    requires { &T::request_keeper_rush; } || requires { &T::default_ai; } ||
    requires { &T::reset_game; } || requires { &T::start_game; };
static_assert(!HasInteractiveAPI<GameEnv>);
template<class T> concept HasRuntimeAccess =
    requires { &T::simulation; } || requires { &T::simulation_; } ||
    requires { &T::match; } || requires { &T::get_state; } || requires { &T::set_state; };
static_assert(!HasRuntimeAccess<GameEnv>);

namespace {
namespace fixtures = football::app::fixtures;
namespace model = football::model;
void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
template<class F> void LogicError(F call) {
  bool threw = false;
  try { call(); } catch (const std::logic_error&) { threw = true; }
  Require(threw, "invalid lifecycle call did not throw");
}
GameEnv MakeGame(MatchOptions options = {}) {
  return {fixtures::MakeDefaultHomeTeam(), fixtures::MakeDefaultAwayTeam(),
          model::MakeLegacyPitch(), options, {}};
}
bool Same(const blunted::Vector3& a, const blunted::Vector3& b) {
  return std::memcmp(a.coords, b.coords, sizeof(a.coords)) == 0;
}
void SameWorld(const WorldState& a, const WorldState& b) {
  Require(a.tick == b.tick && a.phase == b.phase && a.match_time_ms == b.match_time_ms &&
      a.reset_sequence == b.reset_sequence && Same(a.ball_position, b.ball_position) &&
      Same(a.ball_velocity, b.ball_velocity) && a.in_play == b.in_play &&
      a.in_set_piece == b.in_set_piece && a.restart == b.restart &&
      a.restart_taker == b.restart_taker && a.ball_retainer == b.ball_retainer &&
      a.players.size() == b.players.size(), "world mismatch");
  for (int side = 0; side < 2; ++side)
    Require(a.teams[side].score == b.teams[side].score, "score mismatch");
  for (std::size_t i = 0; i < a.players.size(); ++i) {
    const auto& x = a.players[i]; const auto& y = b.players[i];
    Require(x.id == y.id && x.side == y.side && x.active == y.active &&
        x.has_possession == y.has_possession && x.lazy == y.lazy && x.max_speed == y.max_speed &&
        Same(x.position, y.position) && Same(x.velocity, y.velocity) && Same(x.facing, y.facing),
        "player mismatch");
  }
}
void CoreAPI() {
  auto game = MakeGame();
  Require(!game.Finished(), "stopped is not finished");
  LogicError([&] { game.Step(); }); LogicError([&] { game.Observe(); });
  LogicError([&] { game.Result(); });
  game.Stop(); game.Stop(); game.Start();
  LogicError([&] { game.Start(); }); LogicError([&] { game.Result(); });
  const auto initial = game.Observe();
  Require(initial.phase == MatchPhase::PreMatch && initial.match_time_ms == 0 &&
          initial.tick == 0 && initial.players.size() == 22, "invalid initial observation");
  for (int tick = 0; tick < 100; ++tick) game.Step();
  const auto advanced = game.Observe();
  Require(advanced.tick == 100 && initial.tick == 0, "one-step/owning observation boundary");
  game.Stop(); Require(!game.Finished(), "Stop fabricated full time");
  LogicError([&] { game.Result(); });
  game.Start(); SameWorld(game.Observe(), initial);
  Require(game.Observe().simulation_epoch != initial.simulation_epoch, "restart reused epoch");
  for (int tick = 0; tick < 100; ++tick) game.Step();
  SameWorld(game.Observe(), advanced);
  auto peer = MakeGame(); peer.Start();
  for (int tick = 0; tick < 150; ++tick) {
    game.Step();
    if (tick == 40) { peer.Stop(); peer.Start(); }
    if (tick % 3 == 0) peer.Step();
  }
  peer.Stop(); game.Step();
  Require(game.Observe().tick == 251, "peer teardown damaged runner");
}
void Composition() {
  auto home = fixtures::MakeDefaultHomeTeam(); const auto away = fixtures::MakeDefaultAwayTeam();
  home.players[0].id = model::kInvalidPlayerId - 1;
  home.players[1].height = 1.93f;
  home.players[1].attributes.set(model::PlayerStat::physical_velocity, 0.8123456f);
  const auto declared = home;
  MatchOptions options; options.game_engine_random_seed = 123; options.reverse_team_processing = true;
  football::ai::AIConfig config;
  config.initial_tactics[0] = football::ai::MakeTacticalBoard(home, model::TeamSide::Home, model::MakeLegacyPitch());
  config.initial_tactics[0]->width = 0.9f;
  const auto initial_config = config;
  GameEnv game{home, away, model::MakeLegacyPitch(), options, config};
  home.players[0].id = 42; home.players[1].height = 1.5f;
  config.initial_tactics[0]->width = 0.1f; options.game_engine_random_seed = 999;
  for (int repeat = 0; repeat < 2; ++repeat) {
    game.Start();
    Simulation reference;
    MatchOptions expected; expected.game_engine_random_seed = 123; expected.reverse_team_processing = true;
    reference.Init(declared, away, model::MakeLegacyPitch(), expected, false);
    football::ai::DefaultAI policy(declared, away, model::MakeLegacyPitch(), initial_config);
    for (int tick = 0; tick < 600; ++tick) {
      SameWorld(game.Observe(), reference.Observe());
      game.Step(); football::test::StepDefaultAI(reference, policy);
    }
    SameWorld(game.Observe(), reference.Observe()); game.Stop();
  }
}
void FinalResult() {
  MatchOptions options; options.half_duration_ms = 1800;
  auto game = MakeGame(options); game.Start();
  std::uint64_t steps = 0;
  bool first = false, second = false;
  while (!game.Finished()) {
    const auto world = game.Observe();
    first |= world.phase == MatchPhase::FirstHalf; second |= world.phase == MatchPhase::SecondHalf;
    game.Step(); Require(++steps < 1000, "short match failed to finish");
  }
  const auto world = game.Observe(); const auto result = game.Result();
  Require(first && second && world.phase == MatchPhase::Finished && !world.in_play &&
      !world.in_set_piece && world.match_time_ms >= 3600 && result.duration_ticks == steps &&
      result.home_score == world.teams[0].score && result.away_score == world.teams[1].score,
      "result was not authoritative regulation completion");
  for (int tick = 0; tick < 20; ++tick) game.Step();
  SameWorld(game.Observe(), world); Require(game.Result() == result, "final result drifted");
  game.Stop(); LogicError([&] { game.Result(); });
  game.Start(); while (!game.Finished()) game.Step();
  Require(game.Result() == result, "full-match restart not deterministic");
}
void RejectedStartup() {
  auto home = fixtures::MakeDefaultHomeTeam(); auto away = fixtures::MakeDefaultAwayTeam();
  away.players.front().id = home.players.front().id;
  GameEnv rejected{home, away, model::MakeLegacyPitch(), {}, {}};
  for (int attempt = 0; attempt < 2; ++attempt) {
    bool threw = false;
    try { rejected.Start(); } catch (const std::invalid_argument&) { threw = true; }
    Require(threw && !rejected.Finished(), "rejected startup published partial runtime");
    LogicError([&] { rejected.Observe(); }); LogicError([&] { rejected.Result(); });
  }
  rejected.Stop(); rejected.Stop();
}
}
int main() {
  try {
    CoreAPI(); Composition(); FinalResult(); RejectedStartup();
    std::cout << "football_gameenv_test: PASS\n"; return 0;
  } catch (const std::exception& error) {
    std::cerr << "football_gameenv_test: FAIL: " << error.what() << '\n'; return 1;
  }
}
