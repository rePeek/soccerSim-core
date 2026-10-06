#include <algorithm>
#include <array>
#include <cstring>
#include <type_traits>

#include <catch2/catch_test_macros.hpp>

#include "default_ai_fixture.hpp"
#include "app/fixtures/default_teams.hpp"
#include "app/input/grf/input.hpp"
#include "env/game_env.hpp"
#include "sim/match.hpp"
#include "sim/player/player_control_builder.hpp"
#include "sim/team.hpp"

namespace {
using blunted::Vector3;
struct Runtime {
  Simulation simulation;
  football::ai::DefaultAI policy;
  explicit Runtime(bool reverse = false) {
    MatchOptions options;
    options.reverse_team_processing = reverse;
    simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
                    football::app::fixtures::MakeDefaultAwayTeam(),
                    football::model::MakeLegacyPitch(), options, false);
    policy = football::test::MakeDefaultAI(simulation);
  }
  Match *match() { return simulation.match(); }
};

bool Same(const Vector3 &a, const Vector3 &b) {
  return std::memcmp(a.coords, b.coords, sizeof(a.coords)) == 0;
}

const WorldPlayerState &Actor(const WorldState &world, football::model::PlayerId id) {
  auto found = std::find_if(world.players.begin(), world.players.end(),
      [id](const auto &entry) { return entry.id == id; });
  REQUIRE(found != world.players.end());
  return *found;
}
}  // namespace

static_assert(std::is_default_constructible_v<Simulation>);
static_assert(!football::test::HasDecisionObject<Player>);
static_assert(!football::test::HasDecisionObject<Team>);

TEST_CASE("simulation has no implicit AI command source", "[sim][boundary]") {
  Runtime runtime;
  for (int tick = 0; tick < 300; ++tick) runtime.simulation.Step(PlayerControlSet{});
  REQUIRE(runtime.simulation.Observe().tick == 300);
  for (int side = 0; side < 2; ++side) {
    for (Player *player : runtime.match()->GetTeam(side)->GetAllPlayers()) {
      PlayerCommandQueue queue;
      player->RequestCommand(queue);
      REQUIRE(queue.size() == 1);
      REQUIRE(queue[0].desiredFunctionType == e_FunctionType_Movement);
      REQUIRE(queue[0].desiredVelocityFloat == 0.f);
    }
  }
}

TEST_CASE("control translation resolves identity and converts the pitch frame", "[sim][control]") {
  for (bool reverse : {false, true}) {
    Runtime runtime(reverse);
    runtime.simulation.Step(PlayerControlSet{});
    for (int side = 0; side < 2; ++side) {
      const auto &players = runtime.match()->GetTeam(side)->GetAllPlayers();
      Player *player = players[1];
      PlayerControl control;
      control.player = player->GetID();
      control.move_direction = Vector3(1, 0, 0);
      control.look_at = Vector3(20, 4, 3);
      control.desired_speed = 2.5f;
      control.action = ControlAction::ShortPass;
      control.target_player = players[2]->GetID();
      control.target_position = Vector3(30, 10, 0);
      control.power = 0.4f;
      const PlayerControl retained = control;
      auto queue = BuildPlayerCommands(control, *player);
      REQUIRE(queue.size() == 2);
      REQUIRE(queue[0].desiredFunctionType == e_FunctionType_ShortPass);
      REQUIRE(queue[1].desiredFunctionType == e_FunctionType_Movement);
      REQUIRE(queue[0].touchInfo.forcedTargetPlayer == players[2]);
      REQUIRE(queue[0].touchInfo.targetPlayer == players[2]);
      const int expected_x = side == 0 ? 1 : -1;
      REQUIRE(queue[1].desiredDirection.coords[0] == expected_x);
      REQUIRE(queue[1].desiredLookAt.coords[0] == expected_x * 20.f);
      REQUIRE(queue[1].desiredLookAt.coords[2] == 0.f);
      REQUIRE(queue[1].desiredVelocityFloat == control.desired_speed);
      REQUIRE(Same(control.move_direction, retained.move_direction));
      REQUIRE(Same(*control.look_at, *retained.look_at));
      const Vector3 target = *control.target_position * Vector3(expected_x, expected_x, 1);
      REQUIRE(Same(queue[0].touchInfo.inputDirection,
                   (target - player->GetPosition()).GetNormalized(queue[1].desiredDirection)));
      REQUIRE(queue[0].touchInfo.desiredDirection.coords[2] > 0.f);
      REQUIRE(queue[0].touchInfo.desiredPower > 0.f);
      players[2]->SendOff();
      queue = BuildPlayerCommands(control, *player);
      REQUIRE(queue[0].touchInfo.forcedTargetPlayer == nullptr);
      control.target_player = player->GetID();
      queue = BuildPlayerCommands(control, *player);
      REQUIRE(queue[0].touchInfo.forcedTargetPlayer == nullptr);
    }
  }
}

TEST_CASE("observations are owned values in a common home pitch frame", "[sim][observation]") {
  for (bool reverse : {false, true}) {
    Runtime runtime(reverse);
    runtime.simulation.Step(PlayerControlSet{});
    const WorldState initial = runtime.simulation.Observe();
    REQUIRE(initial.teams[0].defending_direction == -1);
    REQUIRE(initial.teams[1].defending_direction == 1);
    REQUIRE(initial.players[0].position.coords[0] < -40.f);
    REQUIRE(initial.players[11].position.coords[0] > 40.f);
    REQUIRE(initial.restart == e_GameMode_KickOff);
    REQUIRE(initial.restart_taker.has_value());
    REQUIRE(initial.pitch == runtime.match()->pitch());
    const auto taker = *initial.restart_taker;
    REQUIRE(runtime.match()->GetReferee()->GetBuffer().taker->GetID() == taker);
    for (int tick = 0; tick < 210; ++tick) football::test::StepDefaultAI(runtime.simulation, runtime.policy);
    const auto world = runtime.simulation.Observe();
    REQUIRE(world.in_play);
    REQUIRE_FALSE(world.in_set_piece);
    REQUIRE(world.restart == e_GameMode_Normal);
    REQUIRE_FALSE(world.restart_taker.has_value());
    REQUIRE(initial.tick == 1);
    REQUIRE(initial.restart_taker == taker);
    REQUIRE(Same(initial.ball_position, Vector3(0, 0, 0.11f)));
  }
}


TEST_CASE("save requests cannot bypass keeper hands legality", "[sim][control]") {
  Runtime runtime;
  Team *team = runtime.match()->GetTeam(0);
  Player *keeper = team->GetGoalie();
  PlayerControl save;
  save.action = ControlAction::Save;
  runtime.match()->GetBall()->ResetSituation(Vector3(-52, 0, 0));
  REQUIRE(BuildPlayerCommands(save, *keeper)[0].desiredFunctionType == e_FunctionType_Deflect);
  REQUIRE(BuildPlayerCommands(save, *team->GetAllPlayers()[1])[0].desiredFunctionType == e_FunctionType_Movement);
  runtime.match()->GetBall()->ResetSituation(Vector3(0));
  REQUIRE(BuildPlayerCommands(save, *keeper)[0].desiredFunctionType == e_FunctionType_Movement);
  runtime.match()->GetBall()->ResetSituation(Vector3(-52, 0, 0));
  team->SetLastTouchPlayer(team->GetAllPlayers()[1], e_TouchType_Intentional_Kicked);
  REQUIRE(BuildPlayerCommands(save, *keeper)[0].desiredFunctionType == e_FunctionType_Movement);
}

TEST_CASE("rules prepare and release restarts through value controls without AI objects", "[sim][rules]") {
  for (e_GameMode mode : {e_GameMode_Corner, e_GameMode_GoalKick, e_GameMode_ThrowIn}) {
    Runtime runtime;
    for (int tick = 0; tick < 300; ++tick) runtime.simulation.Step(PlayerControlSet{});
    Match *match = runtime.match();
    REQUIRE(match->IsInPlay());
    REQUIRE_FALSE(match->IsInSetPiece());
    const int last_team = mode == e_GameMode_Corner ? 1 : 0;
    match->GetTeam(last_team)->SetLastTouchPlayer(
        match->GetTeam(last_team)->GetAllPlayers()[1], e_TouchType_Accidental);
    match->GetBall()->ResetSituation(mode == e_GameMode_ThrowIn
        ? Vector3(10, 37, 0) : Vector3(56, 10, 0));
    Referee *rules = match->GetReferee();
    rules->Process();
    const auto scheduled = rules->GetBuffer();
    REQUIRE(scheduled.active);
    REQUIRE(scheduled.desiredSetPiece == mode);
    REQUIRE_FALSE(match->IsInPlay());
    REQUIRE(scheduled.taker == nullptr);
    REQUIRE_FALSE(runtime.simulation.Observe().restart_taker.has_value());
    REQUIRE(match->GetTeam(0)->GetPieceTaker() == nullptr);
    REQUIRE(match->GetTeam(1)->GetPieceTaker() == nullptr);
    while (match->GetActualTime_ms() <= scheduled.prepareTime)
      runtime.simulation.Step(PlayerControlSet{});
    const auto world = runtime.simulation.Observe();
    REQUIRE(world.restart == mode);
    REQUIRE(world.restart_taker.has_value());
    REQUIRE(rules->GetBuffer().taker != nullptr);
    REQUIRE(match->GetTeam(scheduled.teamID)->GetPieceTaker() == rules->GetBuffer().taker);
    REQUIRE(match->GetTeam(1 - scheduled.teamID)->GetPieceTaker() == nullptr);
    REQUIRE(rules->GetBuffer().startTime == scheduled.startTime);
    while (match->GetActualTime_ms() <= scheduled.startTime)
      runtime.simulation.Step(PlayerControlSet{});
    REQUIRE(match->IsInPlay());
    for (int tick = 0; tick < 800 && match->IsInSetPiece(); ++tick)
      football::test::StepDefaultAI(runtime.simulation, runtime.policy);
    Player *taker = rules->GetBuffer().taker;
    CAPTURE(mode, taker->GetPosition().coords[0], taker->GetPosition().coords[1],
            taker->GetCurrentFunctionType(), match->GetBall()->Predict(0).coords[0],
            match->GetBall()->Predict(0).coords[1]);
    REQUIRE_FALSE(match->IsInSetPiece());
    REQUIRE_FALSE(rules->GetBuffer().active);
    REQUIRE(match->GetTeam(0)->GetSetPieceType() == e_GameMode_Normal);
    REQUIRE(match->GetTeam(1)->GetPieceTaker() == nullptr);
  }
}

TEST_CASE("explicit controls are the sole command source and absence is idle", "[sim][control]") {
  Runtime runtime;
  Player *player = runtime.match()->GetTeam(0)->GetAllPlayers()[1];
  PlayerControl control;
  control.move_direction = Vector3(1, 0, 0);
  control.desired_speed = 1.234f;
  PlayerControlSet controls;
  controls.Set(player->GetID(), control);
  runtime.simulation.Step(controls);
  PlayerCommandQueue queue;
  player->RequestCommand(queue);
  REQUIRE(queue.size() == 1);
  REQUIRE(queue[0].desiredVelocityFloat == control.desired_speed);
  REQUIRE(Same(queue[0].desiredDirection, control.move_direction));
  // Inputs are owned values; an empty next tick does not retain the old command.
  runtime.simulation.Step(PlayerControlSet{});
  player->RequestCommand(queue);
  REQUIRE(queue.size() == 1);
  REQUIRE(queue[0].desiredVelocityFloat == 0.f);
}

TEST_CASE("GRF environment input and plain control replay execute the same simulation", "[sim][input][replay]") {
  const auto home = football::app::fixtures::MakeDefaultHomeTeam();
  const auto away = football::app::fixtures::MakeDefaultAwayTeam();
  const auto pitch = football::model::MakeLegacyPitch();
  GameEnv environment(home, away, pitch);
  environment.start_game();
  Simulation replay;
  replay.Init(home, away, pitch, MatchOptions{}, false);
  football::app::grf::Input input(home, football::model::TeamSide::Home);
  using Action = football::app::grf::Action;
  const auto same_world = [](const WorldState &a, const WorldState &b) {
    REQUIRE(a.tick == b.tick);
    REQUIRE(a.reset_sequence == b.reset_sequence);
    REQUIRE(Same(a.ball_position, b.ball_position));
    REQUIRE(Same(a.ball_velocity, b.ball_velocity));
    REQUIRE(a.restart_taker == b.restart_taker);
    REQUIRE(a.players.size() == b.players.size());
    for (std::size_t i = 0; i < a.players.size(); ++i) {
      REQUIRE(a.players[i].id == b.players[i].id);
      REQUIRE(Same(a.players[i].position, b.players[i].position));
      REQUIRE(Same(a.players[i].velocity, b.players[i].velocity));
      REQUIRE(Same(a.players[i].facing, b.players[i].facing));
    }
  };
  std::vector<PlayerControlSet> tape;
  for (int tick = 0; tick < 400; ++tick) {
    if (tick == 0) input.Apply(Action::Right);
    if (tick == 100) input.Apply(Action::Sprint);
    if (tick == 220) input.Apply(Action::Shot);
    if (tick == 230) input.Apply(Action::Pressure);
    if (tick == 240) input.Apply(Action::TeamPressure);
    if (tick == 250) input.Apply(Action::KeeperRush);
    if (tick == 270) input.Apply(Action::ReleasePressure);
    if (tick == 280) input.Apply(Action::BuiltinAI);
    if (tick == 300) input.Apply(Action::Left);
    const auto world = environment.observe();
    input.Update(world, environment.default_ai(), environment.controls());
    PlayerControlSet compiled;
    environment.default_ai().Update(world, compiled);
    for (const auto &control : environment.controls().controls()) compiled.Set(control.player, control);
    tape.push_back(compiled);
    environment.step();
    replay.Step(compiled);
    same_world(environment.observe(), replay.Observe());
  }
  const auto expected = replay.Observe();
  const auto rng = replay.match()->rng().engine();
  replay.Stop();
  replay.Init(home, away, pitch, MatchOptions{}, false);
  for (const auto &frame : tape) replay.Step(frame);
  same_world(expected, replay.Observe());
  REQUIRE(replay.match()->rng().engine() == rng);
}

TEST_CASE("world discontinuities invalidate AI requests without storing them in sim", "[sim][boundary]") {
  Runtime runtime;
  for (int tick = 0; tick < 300; ++tick) football::test::StepDefaultAI(runtime.simulation, runtime.policy);
  const auto before = runtime.simulation.Observe();
  REQUIRE(runtime.policy.RequestAttackingRun(football::model::TeamSide::Home, before, 7));
  runtime.match()->ResetSituation(Vector3(0));
  const auto after = runtime.simulation.Observe();
  REQUIRE(after.tick == before.tick);
  REQUIRE(after.reset_sequence == before.reset_sequence + 1);
  REQUIRE_FALSE(runtime.policy.requests(football::model::TeamSide::Home).attacking_run.Active(
      after.tick, after.reset_sequence));
  REQUIRE(before.reset_sequence + 1 == runtime.match()->GetResetSequence());
}
