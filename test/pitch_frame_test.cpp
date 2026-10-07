#include <algorithm>
#include <cstring>
#include <array>
#include <tuple>

#include <catch2/catch_test_macros.hpp>

#include "app/fixtures/default_teams.hpp"
#include "default_ai_fixture.hpp"
#include "sim/match/match.hpp"
#include "sim/observation/pitch_frame.hpp"
#include "sim/player/player_control_builder.hpp"
#include "sim/simulation.hpp"
#include "sim/team/team.hpp"

namespace {
using blunted::Vector3;

void Init(Simulation& simulation, bool reverse,
          football::sim::TickSpan half_duration = football::sim::TickSpan{180}) {
  MatchOptions options;
  options.reverse_team_processing = reverse;
  options.half_duration = half_duration;
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(),
      football::model::MakeLegacyPitch(), options);
}

void ReachHalf(Simulation& simulation, MatchPhase phase) {
  const auto policy = football::test::MakeDefaultAI(simulation);
  for (int step = 0; step < 3000; ++step) {
    if (simulation.Observe().phase == phase && simulation.Observe().ball_in_play &&
        !simulation.Observe().in_set_piece) return;
    REQUIRE_FALSE(simulation.Finished());
    football::test::StepDefaultAI(simulation, policy);
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
    Init(simulation, reverse, football::sim::Minutes(3));
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

TEST_CASE("native reachability targets the nearby real ball in either actor-processing frame",
          "[sim][pitch-frame][reachability]") {
  for (bool reverse : {false, true}) {
    Simulation simulation;
    Init(simulation, reverse);
    for (auto phase : {MatchPhase::FirstHalf, MatchPhase::SecondHalf}) {
      ReachHalf(simulation, phase);
      Match& match = *simulation.match();
      // Clear previous action/perception state, but preserve accepted live play.
      match.ResetSituation(Vector3(0));
      for (int team = 0; team < 2; ++team) {
        for (Player* player : match.GetTeam(team)->GetAllPlayers())
          SetPhysicalPlayer(*player, Vector3(0.f, team == 0 ? 25.f : -25.f, 0.f));
        SetPhysicalPlayer(*match.GetTeam(team)->GetAllPlayers()[1],
            Vector3(23.75f, team == 0 ? 7.f : 6.25f, 0.f));
      }
      SetPhysicalBall(match, Vector3(23.f, 7.f, 0.11f), Vector3(0));
      for (int step = 0; step < 10; ++step) simulation.Step({}); // All native refresh phases.
      const auto world = simulation.Observe();
      for (int team = 0; team < 2; ++team) {
        const auto* nearby = match.GetTeam(team)->GetAllPlayers()[1];
        CAPTURE(reverse, phase, team, nearby->GetTimeNeededToGetToBall_ms());
        REQUIRE((Actor(world, nearby->GetID()).position - world.ball_position).GetLength() < 1.2f);
        // The native procedural reach search must not exhaust its 3-second
        // horizon on the ghost mirror 49 m away. No AI score target is involved.
        REQUIRE(nearby->GetTimeNeededToGetToBall_ms() <
            football::sim::ToMilliseconds(football::sim::ball_timing::kPredictionHorizon));
      }
    }
  }
}

TEST_CASE("both processing orders execute native open-play contacts in both halves and replay",
          "[sim][pitch-frame][replay]") {
  using namespace football::sim;
  for (bool reverse : {false, true}) {
    const auto run = [reverse] {
      Simulation simulation;
      Init(simulation, reverse, TickSpan{5000});
      const auto policy = football::test::MakeDefaultAI(simulation);
      std::vector<Tick> touches(simulation.Observe().players.size());
      std::vector<std::tuple<MatchPhase, football::model::PlayerId, Tick>> contacts;
      std::array<unsigned, 2> per_half{};
      unsigned steps = 0;
      while (!simulation.Finished()) {
        const auto before = simulation.Observe();
        football::test::StepDefaultAI(simulation, policy);
        REQUIRE(++steps < 14000);
        std::size_t index = 0;
        for (int team = 0; team < 2; ++team) {
          for (auto* player : simulation.match()->GetTeam(team)->GetAllPlayers()) {
            const auto touch = player->GetLastTouchTick();
            if (before.ball_in_play && !before.in_set_piece && touch != touches[index] &&
                player->GetLastTouchType() == e_TouchType_Intentional_Kicked) {
              contacts.emplace_back(before.phase, player->GetID(), touch);
              ++per_half[before.phase == MatchPhase::FirstHalf ? 0 : 1];
            }
            touches[index++] = touch;
          }
        }
      }
      CAPTURE(reverse, per_half[0], per_half[1]);
      // Progress is accepted native contact away from restarts, not a score or
      // desired effective-time target. The old reverse frame produces none.
      REQUIRE(per_half[0] > 0);
      REQUIRE(per_half[1] > 0);
      const auto world = simulation.Observe();
      REQUIRE(world.regulation_time == TickSpan{10000});
      REQUIRE(world.ball_in_play_time <= world.regulation_time);
      return std::make_tuple(contacts, world.tick, world.ball_in_play_time,
          world.teams[0].score, world.teams[1].score, simulation.match()->rng().engine());
    };
    const auto first = run();
    REQUIRE(run() == first);
  }
}
