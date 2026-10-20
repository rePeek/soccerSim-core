// End-of-period lifecycle and the coordinate-frame contract at the half-time
// change of ends.
//
// These are bounded Runtime integration tests, not leaf unit tests: they reach
// the real `EndPeriod` gate through `Step`, so the pending flag, the next-tick
// physical swap and the scheduled second-half kickoff are exercised as one
// chain instead of by calling the frame mutators directly. The tick budget
// stays tiny (two players a side, a two-tick half) because the frame logic does
// not depend on roster size.
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <utility>
#include <variant>
#include <vector>

#include "football/ball/ball_model.hpp"
#include "football/observation/world_state.hpp"
#include "football/pitch/pitch.hpp"
#include "football/runtime/match_config.hpp"
#include "football/runtime/simulation_config.hpp"
#include "player/player.hpp"
#include "runtime/simulation.hpp"
#include "testing/simulation_access.hpp"

namespace {

using football::sim::testing::SimulationAccess;
using namespace football::sim;
using namespace football::sim::event;

// Two players a side: enough for a live roster, cheap enough that the bounded
// suite never pays for a full 11v11 Step to prove a coordinate contract.
TeamSetup SmallTeam(football::player::PlayerId first) {
  TeamSetup team;
  for (int i = 0; i < 2; ++i) {
    football::player::PlayerDefinition player;
    player.id = first + static_cast<football::player::PlayerId>(i);
    player.height = 1.75f;
    team.players.push_back(std::move(player));
  }
  team.tactical_formation = {{{-1.0f, 0.0f}, e_PlayerRole_GK},
                             {{0.4f, 0.0f}, e_PlayerRole_CF}};
  return team;
}

void Init(Simulation& sim, bool reverse, TickSpan half) {
  MatchOptions options;
  options.reverse_team_processing = reverse;
  options.half_duration = half;
  options.snapshot_capacity = 8;
  sim.Init(SmallTeam(0), SmallTeam(2), football::pitch::Pitch{}, options,
           football::ball::BallModel{});
}

// Reach an underway half without the opening ceremony. The change-of-ends gate
// only needs a running regulation clock and an authorized play state; the
// ceremony would dominate the bounded tick budget without adding coverage.
void OpenHalf(Simulation& sim, MatchPhase phase) {
  SimulationAccess::ClearRestart(sim);
  SimulationAccess::SetPhase(sim, phase);
  SimulationAccess::StartPlay(sim);
  SimulationAccess::StartBallInPlay(sim);
}

template <typename T>
std::size_t CountEvents(const Simulation& sim) {
  std::size_t count = 0;
  for (const MatchEventRecord& record : SimulationAccess::MatchEventsOf(sim)) {
    if (std::holds_alternative<T>(record.event)) ++count;
  }
  return count;
}

const RestartAwardedEvent* LastAward(const Simulation& sim) {
  const RestartAwardedEvent* result = nullptr;
  for (const MatchEventRecord& record : SimulationAccess::MatchEventsOf(sim)) {
    if (const auto* award = std::get_if<RestartAwardedEvent>(&record.event)) {
      result = award;
    }
  }
  return result;
}

}  // namespace

TEST_CASE("Runtime harness: half time swaps ends exactly once at the next tick",
          "[runtime][sim]") {
  for (const bool reverse : {false, true}) {
    CAPTURE(reverse);
    Simulation sim;
    Init(sim, reverse, TickSpan{2});
    OpenHalf(sim, MatchPhase::FirstHalf);
    REQUIRE(SimulationAccess::IsHalfUnderwayOf(sim));

    const football::team::Team& home_team = SimulationAccess::TeamOf(sim, 0);
    const football::team::Team& away_team = SimulationAccess::TeamOf(sim, 1);
    const int home_static = SimulationAccess::TeamFrameOf(sim, 0).static_side;
    const int away_static = SimulationAccess::TeamFrameOf(sim, 1).static_side;
    const int home_dynamic = SimulationAccess::TeamFrameOf(sim, 0).dynamic_side;
    const int away_dynamic = SimulationAccess::TeamFrameOf(sim, 1).dynamic_side;
    // Baseline: the persistent ends differ, both execution frames do not.
    REQUIRE(home_static == -1);
    REQUIRE(away_static == 1);
    REQUIRE(home_dynamic == -1);
    REQUIRE(away_dynamic == -1);

    PlayerControlSet controls;
    int guard = 0;
    while (SimulationAccess::PhaseOf(sim) != MatchPhase::SecondHalf && guard < 16) {
      sim.Step(controls);
      ++guard;
    }
    REQUIRE(SimulationAccess::PhaseOf(sim) == MatchPhase::SecondHalf);
    REQUIRE(guard < 16);
    // The whistle published the period once and scheduled the second-half
    // kickoff, but the physical change is still pending at the next Step entry.
    REQUIRE(CountEvents<PeriodEndedEvent>(sim) == 1);
    REQUIRE(SimulationAccess::RestartContextOf(sim).kind == e_GameMode_KickOff);
    CHECK(SimulationAccess::TeamFrameOf(sim, 0).static_side == home_static);
    CHECK(SimulationAccess::TeamFrameOf(sim, 1).static_side == away_static);

    // The next Step entry applies the change of ends before counting the tick.
    sim.Step(controls);
    CHECK(SimulationAccess::TeamFrameOf(sim, 0).static_side == -home_static);
    CHECK(SimulationAccess::TeamFrameOf(sim, 1).static_side == -away_static);
    CHECK(SimulationAccess::TeamFrameOf(sim, 0).dynamic_side == -home_dynamic);
    CHECK(SimulationAccess::TeamFrameOf(sim, 1).dynamic_side == -away_dynamic);
    // Identity is a match fact, never a direction, so it does not flip.
    for (Player* player : home_team.Players()) {
      CHECK(player->GetTeamSide() == TeamSide::Home);
    }
    for (Player* player : away_team.Players()) {
      CHECK(player->GetTeamSide() == TeamSide::Away);
    }
    // One period, one real change of ends.
    CHECK(CountEvents<PeriodEndedEvent>(sim) == 1);
    CHECK(sim.Stop());
  }
}

TEST_CASE("Runtime harness: a change of ends preserves the home-pitch observation frame",
          "[runtime][sim]") {
  Simulation sim;
  Init(sim, false, football::sim::Minutes(45));

  // An asymmetric, off-centre ball: a missing or doubled rotation cannot hide.
  football::ball::Ball* ball = SimulationAccess::BallOf(sim);
  football::ball::BallState ball_state = ball->state();
  ball_state.position = football::math::Vector3(3.0f, -4.0f, ball_state.position.coords[2]);
  ball_state.velocity = football::math::Vector3(2.0f, 5.0f, 0.0f);
  ball->Reset(ball_state);

  const WorldState before = sim.Observe();
  SimulationAccess::ApplyChangeOfEndsNow(sim);
  const WorldState after = sim.Observe();

  // The ball representation and the execution frame rotate together, so the
  // published home-pitch coordinates are exactly unchanged.
  CHECK(after.ball_position == before.ball_position);
  CHECK(after.ball_velocity == before.ball_velocity);
  REQUIRE(after.players.size() == before.players.size());
  for (std::size_t i = 0; i < before.players.size(); ++i) {
    CHECK(after.players[i].id == before.players[i].id);
    CHECK(after.players[i].side == before.players[i].side);
    CHECK(after.players[i].position == before.players[i].position);
    CHECK(after.players[i].velocity == before.players[i].velocity);
  }
  // The published defending directions are a fixed home-pitch contract: they
  // describe the frame, not the physical ends, so they never flip.
  CHECK(after.teams[0].defending_direction == -1);
  CHECK(after.teams[1].defending_direction == 1);
  CHECK(sim.Stop());
}

TEST_CASE("Runtime harness: a goal after the change of ends credits the new attack direction",
          "[runtime][sim]") {
  Simulation sim;
  Init(sim, false, football::sim::Minutes(45));
  OpenHalf(sim, MatchPhase::SecondHalf);
  SimulationAccess::ApplyChangeOfEndsNow(sim);
  // Home now defends +X and attacks -X; Away is the mirror image.
  REQUIRE(SimulationAccess::TeamFrameOf(sim, 0).static_side == 1);
  REQUIRE(SimulationAccess::TeamFrameOf(sim, 1).static_side == -1);
  REQUIRE(SimulationAccess::IsBallInPlayOf(sim));

  // Home's last touch, then the ball crosses the -X goal mouth it now attacks.
  SimulationAccess::TouchesOf(sim).Record(0, 1, e_TouchType_Intentional_Kicked);
  football::ball::Ball* ball = SimulationAccess::BallOf(sim);
  football::ball::BallState state = ball->state();
  state.position = football::math::Vector3(-52.45f, 0.0f, 0.6f);
  state.velocity = football::math::Vector3(-40.0f, 0.0f, 0.0f);
  ball->Reset(state);
  sim.Step(PlayerControlSet{});

  CHECK(SimulationAccess::ScoreOf(sim, 0) == 1);
  CHECK(SimulationAccess::ScoreOf(sim, 1) == 0);
  CHECK(SimulationAccess::LastGoalTeamOf(sim) == 0);
  CHECK(sim.Stop());
}

TEST_CASE("Runtime harness: the goal-line restart side follows the physical ends",
          "[runtime][sim]") {
  Simulation sim;
  Init(sim, false, football::sim::Minutes(45));
  MatchPhase phase = MatchPhase::FirstHalf;
  e_GameMode expected = e_GameMode_GoalKick;
  SECTION("first half: the +X line is the opponent's goal line") {
    phase = MatchPhase::FirstHalf;
    expected = e_GameMode_GoalKick;
  }
  SECTION("second half: the +X line is Home's own goal line") {
    phase = MatchPhase::SecondHalf;
    expected = e_GameMode_Corner;
  }
  OpenHalf(sim, phase);
  if (phase == MatchPhase::SecondHalf) SimulationAccess::ApplyChangeOfEndsNow(sim);

  SimulationAccess::TouchesOf(sim).Record(0, 1, e_TouchType_Intentional_Kicked);
  SimulationAccess::ResetBall(sim, football::math::Vector3(55.0f, 40.0f, 0.0f));
  SimulationAccess::ProcessReferee(sim);

  const RestartAwardedEvent* award = LastAward(sim);
  REQUIRE(award != nullptr);
  // Away always takes the restart: in the first half the ball left over the
  // line Home defends at -X, in the second half over the line Home defends at
  // +X. The verdict follows the ends, not the Home/Away label.
  CHECK(award->kind == expected);
  CHECK(award->team == TeamSide::Away);
  CHECK(sim.Stop());
}

TEST_CASE("Runtime harness: the published frame conversions compose to a fixed offset",
          "[runtime][sim]") {
  for (const bool reverse : {false, true}) {
    CAPTURE(reverse);
    Simulation sim;
    Init(sim, reverse, football::sim::Minutes(45));
    // An asymmetric raw ball so a wrong or doubled rotation cannot hide.
    SimulationAccess::ResetBall(sim, football::math::Vector3(3.5f, -2.5f, 0.0f));

    for (int half = 0; half < 2; ++half) {
      const football::ball::BallState raw = SimulationAccess::BallOf(sim)->state();
      const auto facts = SimulationAccess::RefereeFactsOf(sim);
      // home = ball_to_home(raw); stadium = stadium_to_home(home).
      const football::math::Vector3 home = facts.ball_to_home.Position(raw.position);
      const football::math::Vector3 stadium = facts.stadium_to_home.Position(home);
      // The between-tick persistent frame differs from the Stadium Frame by
      // exactly the reverse-processing rotation, in both halves.
      const football::math::Vector3 expected =
          PitchFrameTransform(reverse).Position(raw.position);
      CHECK(stadium.coords[0] == expected.coords[0]);
      CHECK(stadium.coords[1] == expected.coords[1]);
      // The published observation is that same home-pitch point.
      const WorldState world = sim.Observe();
      CHECK(world.ball_position.coords[0] == home.coords[0]);
      CHECK(world.ball_position.coords[1] == home.coords[1]);

      if (half == 0) SimulationAccess::ApplyChangeOfEndsNow(sim);
    }
    CHECK(sim.Stop());
  }
}
