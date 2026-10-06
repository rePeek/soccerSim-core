#include <algorithm>
#include <cstring>

#include <catch2/catch_test_macros.hpp>

#include "app/fixtures/default_teams.hpp"
#include "default_ai_fixture.hpp"
#include "sim/match.hpp"
#include "sim/pitch_frame.hpp"
#include "sim/player/player_control_builder.hpp"
#include "sim/simulation.hpp"
#include "sim/team.hpp"

namespace {
using blunted::Vector3;

void Init(Simulation& simulation, bool reverse, std::uint64_t half_duration_ms = 1800) {
  MatchOptions options;
  options.reverse_team_processing = reverse;
  options.half_duration_ms = half_duration_ms;
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(),
      football::model::MakeLegacyPitch(), options);
}

void ReachHalf(Simulation& simulation, MatchPhase phase) {
  for (int step = 0; step < 1000; ++step) {
    if (simulation.Observe().phase == phase && simulation.IsInPlay()) return;
    REQUIRE_FALSE(simulation.Finished());
    simulation.Step({});
  }
  FAIL("requested playing half was not reached");
}

bool Same(const Vector3& a, const Vector3& b) {
  return std::memcmp(a.coords, b.coords, sizeof(a.coords)) == 0;
}

// Independent physical-pitch setup: undo ONLY the legacy processing mirror.
// Do not use the policy transform being tested to generate expected values.
void SetPhysicalBall(Match& match, Vector3 position, Vector3 velocity) {
  if (match.options().reverse_team_processing) {
    position.Mirror();
    velocity.Mirror();
  }
  match.GetBall()->SetPosition(position);
  match.GetBall()->SetMomentum(velocity);
}

Vector3 PhysicalBall(const Match& match) {
  auto position = match.GetBall()->Predict(0);
  if (match.options().reverse_team_processing) position.Mirror();
  return position;
}

void SetPhysicalPlayer(Player& player, Vector3 position) {
  Vector3 focus = position + Vector3(3.f, 4.f, 0.f);
  if (!player.GetTeam()->onOriginalSide()) {
    position.Mirror();
    focus.Mirror();
  }
  player.ResetPosition(position, focus);
}

const WorldPlayerState& Actor(const WorldState& world,
                             football::model::PlayerId id) {
  const auto found = std::find_if(world.players.begin(), world.players.end(),
      [id](const auto& player) { return player.id == id; });
  REQUIRE(found != world.players.end());
  return *found;
}
}  // namespace

TEST_CASE("a physical change of ends preserves canonical ball and player geometry",
          "[sim][pitch-frame]") {
  for (bool reverse : {false, true}) {
    Simulation simulation;
    Init(simulation, reverse, 180000);
    ReachHalf(simulation, MatchPhase::FirstHalf);
    Match& match = *simulation.match();
    SetPhysicalPlayer(*match.GetTeam(0)->GetAllPlayers()[1], Vector3(-30.f, 9.f, 0.f));
    SetPhysicalPlayer(*match.GetTeam(1)->GetAllPlayers()[1], Vector3(27.f, 6.f, 0.f));
    Player& runner = *match.GetTeam(0)->GetAllPlayers()[1];
    PlayerControl control;
    control.move_direction = Vector3(3.f, 4.f, 0.f);
    control.desired_speed = 3.5f;
    PlayerControlSet controls;
    controls.Set(runner.GetID(), control);
    for (int step = 0; step < 30; ++step) simulation.Step(controls);
    SetPhysicalBall(match, Vector3(23.f, 7.f, 0.4f), Vector3(8.f, -2.f, 1.f));
    const auto before = simulation.Observe();
    REQUIRE(Actor(before, runner.GetID()).velocity.GetLength() > 0.f);
    REQUIRE(Same(before.ball_position, Vector3(23.f, 7.f, 0.4f)));
    REQUIRE(Same(before.ball_velocity, Vector3(8.f, -2.f, 1.f)));
    const auto rng = match.rng().engine();

    // Exercise the spatial operations used by Match::SwitchEnds without an
    // intervening physics tick or kickoff reset. Deliberately do not change the
    // phase enum: orientation must come from runtime sides, not SecondHalf.
    for (int change = 0; change < 2; ++change) {
      match.GetTeam(0)->SwitchEnds();
      match.GetTeam(1)->SwitchEnds();
      match.GetBall()->Mirror();
      const auto after = simulation.Observe();
      REQUIRE(after.phase == before.phase);
      REQUIRE(Same(after.ball_position, before.ball_position));
      REQUIRE(Same(after.ball_velocity, before.ball_velocity));
      REQUIRE(after.teams[0].defending_direction == -1);
      REQUIRE(after.teams[1].defending_direction == 1);
      for (const auto& player : before.players) {
        const auto& observed = Actor(after, player.id);
        REQUIRE(Same(observed.position, player.position));
        REQUIRE(Same(observed.velocity, player.velocity));
      }
      REQUIRE(match.rng().engine() == rng);
    }
  }
}

TEST_CASE("player ball distance survives projection in both halves and processing orders",
          "[sim][pitch-frame]") {
  for (bool reverse : {false, true}) {
    Simulation simulation;
    Init(simulation, reverse);
    for (auto phase : {MatchPhase::FirstHalf, MatchPhase::SecondHalf}) {
      ReachHalf(simulation, phase);
      Match& match = *simulation.match();
      const Vector3 physical_ball(23.f, 7.f, 0.4f);
      const Vector3 physical_velocity(8.f, -2.f, 1.f);
      SetPhysicalBall(match, physical_ball, physical_velocity);
      const auto world = simulation.Observe();
      const bool changed_ends = phase == MatchPhase::SecondHalf;
      auto expected_ball = physical_ball;
      auto expected_velocity = physical_velocity;
      if (changed_ends) {
        expected_ball.Mirror();
        expected_velocity.Mirror();
      }
      CAPTURE(reverse, phase);
      REQUIRE(Same(world.ball_position, expected_ball));
      REQUIRE(Same(world.ball_velocity, expected_velocity));
      REQUIRE(world.teams[0].defending_direction == -1);
      REQUIRE(world.teams[1].defending_direction == 1);
      for (int team = 0; team < 2; ++team) {
        for (Player* player : match.GetTeam(team)->GetAllPlayers()) {
          const auto physical_position = player->GetPitchPosition();
          auto expected_position = physical_position;
          if (changed_ends) expected_position.Mirror();
          const auto& observed = Actor(world, player->GetID());
          REQUIRE(Same(observed.position, expected_position));
          REQUIRE((physical_position - PhysicalBall(match)).GetLength() ==
                  (observed.position - world.ball_position).GetLength());
        }
      }
    }
  }
}

TEST_CASE("canonical controls round trip through each actor runtime frame",
          "[sim][pitch-frame]") {
  for (bool reverse : {false, true}) {
    Simulation simulation;
    Init(simulation, reverse);
    for (auto phase : {MatchPhase::FirstHalf, MatchPhase::SecondHalf}) {
      ReachHalf(simulation, phase);
      for (int team = 0; team < 2; ++team) {
        Player& player = *simulation.match()->GetTeam(team)->GetAllPlayers()[1];
        PlayerControl control;
        control.player = player.GetID();
        control.move_direction = Vector3(3.f, 4.f, 0.f);
        control.look_at = Vector3(23.f, 7.f, 0.4f);
        control.target_position = Vector3(31.f, -11.f, 0.3f);
        control.desired_speed = 3.5f;
        control.action = ControlAction::Shoot;
        control.power = 0.6f;
        const PlayerControl retained = control;
        const bool mirror = player.GetTeam()->GetDynamicSide() != (team == 0 ? -1 : 1);
        auto direction = control.move_direction;
        auto look_at = *control.look_at;
        auto target = *control.target_position;
        if (mirror) { direction.Mirror(); look_at.Mirror(); target.Mirror(); }
        const auto queue = BuildPlayerCommands(control, player);
        REQUIRE(queue.size() == 2);
        REQUIRE(queue[0].desiredFunctionType == e_FunctionType_Shot);
        REQUIRE(queue[1].desiredDirection == direction.Get2D().GetNormalized(player.GetDirectionVec()));
        REQUIRE(queue[1].desiredLookAt == look_at.Get2D());
        REQUIRE(queue[0].touchInfo.inputDirection ==
            (target - player.GetPosition()).GetNormalized(queue[1].desiredDirection));
        REQUIRE(Same(control.move_direction, retained.move_direction));
        REQUIRE(Same(*control.look_at, *retained.look_at));
        REQUIRE(Same(*control.target_position, *retained.target_position));

        const auto to_home = ToHomePitchFrame(*player.GetTeam());
        const auto from_home = FromHomePitchFrame(*player.GetTeam());
        const Vector3 value(-0.0f, 7.f, 0.4f);
        REQUIRE(Same(to_home.Position(from_home.Position(value)), value));
        REQUIRE(Same(to_home.Direction(from_home.Direction(value)), value));
        REQUIRE(to_home.Position(player.GetPosition()) ==
            Actor(simulation.Observe(), player.GetID()).position);
      }
    }
  }
}

TEST_CASE("second half AI chases the real nonzero ball rather than its ghost mirror",
          "[sim][pitch-frame]") {
  for (bool reverse : {false, true}) {
    Simulation simulation;
    Init(simulation, reverse);
    ReachHalf(simulation, MatchPhase::SecondHalf);
    Match& match = *simulation.match();
    for (int team = 0; team < 2; ++team) {
      for (Player* player : match.GetTeam(team)->GetAllPlayers())
        SetPhysicalPlayer(*player, Vector3(0.f, team == 0 ? 30.f : -30.f, 0.f));
    }
    Player& nearby = *match.GetTeam(0)->GetAllPlayers()[1];
    Player& ghost_nearby = *match.GetTeam(0)->GetAllPlayers()[2];
    SetPhysicalPlayer(nearby, Vector3(24.f, 7.f, 0.f));
    SetPhysicalPlayer(ghost_nearby, Vector3(-22.f, -7.f, 0.f));
    SetPhysicalBall(match, Vector3(23.f, 7.f, 0.4f), Vector3(8.f, -2.f, 1.f));
    auto world = simulation.Observe();
    // Isolate spatial chasing from stale pre-positioning possession metadata.
    for (auto& player : world.players) player.has_possession = false;
    world.ball_retainer.reset();
    world.in_set_piece = false;
    world.restart = e_GameMode_Normal;
    world.restart_taker.reset();
    auto policy = football::test::MakeDefaultAI(simulation);
    PlayerControlSet controls;
    policy.Update(world, controls);
    REQUIRE(controls.Get(nearby.GetID()) != nullptr);
    REQUIRE(controls.Get(ghost_nearby.GetID()) != nullptr);
    REQUIRE(controls.Get(nearby.GetID())->action == ControlAction::Trap);
    REQUIRE(controls.Get(ghost_nearby.GetID())->action == ControlAction::None);
    const auto intercept = world.ball_position.Get2D() + world.ball_velocity.Get2D() * 0.18f;
    const auto& observed = Actor(world, nearby.GetID());
    REQUIRE(controls.Get(nearby.GetID())->move_direction ==
        (intercept - observed.position).Get2D().GetNormalized(observed.facing));
  }
}
