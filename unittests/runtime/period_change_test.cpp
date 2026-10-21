// End-of-period lifecycle and the coordinate-frame contract at the half-time
// change of ends.
//
// These are bounded Runtime integration tests, not leaf unit tests: they reach
// the real `EndPeriod` gate through `Step`, so the pending flag, the next-tick
// physical swap and the scheduled second-half kickoff are exercised as one
// chain instead of by calling the frame mutators directly. The tick budget
// stays tiny (two players a side, a two-tick half) because the frame logic does
// not depend on roster size.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
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
    const int home_static =
        SimulationAccess::TeamOrientationOf(sim, 0).defending_direction;
    const int away_static =
        SimulationAccess::TeamOrientationOf(sim, 1).defending_direction;
    // Baseline: the ends differ.
    REQUIRE(home_static == -1);
    REQUIRE(away_static == 1);

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
    CHECK(SimulationAccess::TeamOrientationOf(sim, 0).defending_direction == home_static);
    CHECK(SimulationAccess::TeamOrientationOf(sim, 1).defending_direction == away_static);

    // The next Step entry applies the change of ends before counting the tick.
    sim.Step(controls);
    CHECK(SimulationAccess::TeamOrientationOf(sim, 0).defending_direction == -home_static);
    CHECK(SimulationAccess::TeamOrientationOf(sim, 1).defending_direction == -away_static);
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

TEST_CASE("Runtime harness: a change of ends flips the published directions",
          "[runtime][sim]") {
  Simulation sim;
  Init(sim, false, football::sim::Minutes(45));

  // An asymmetric, off-centre ball: a missing or doubled rotation cannot hide.
  football::ball::Ball* ball = SimulationAccess::BallOf(sim);
  football::ball::BallState ball_state = ball->state();
  ball_state.position = football::math::Vector3(3.0f, -4.0f, ball_state.position.coords[2]);
  ball_state.velocity = football::math::Vector3(2.0f, 5.0f, 0.0f);
  SimulationAccess::SetBallState(sim, ball_state);

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
  // The change of ends flips the published defending directions; the stored
  // positions never move.
  CHECK(after.teams[0].defending_direction == 1);
  CHECK(after.teams[1].defending_direction == -1);
  CHECK(sim.Stop());
}

TEST_CASE("Runtime harness: a goal after the change of ends credits the new attack direction",
          "[runtime][sim]") {
  Simulation sim;
  Init(sim, false, football::sim::Minutes(45));
  OpenHalf(sim, MatchPhase::SecondHalf);
  SimulationAccess::ApplyChangeOfEndsNow(sim);
  // Home now defends +X and attacks -X; Away is the mirror image.
  REQUIRE(SimulationAccess::TeamOrientationOf(sim, 0).defending_direction == 1);
  REQUIRE(SimulationAccess::TeamOrientationOf(sim, 1).defending_direction == -1);
  REQUIRE(SimulationAccess::IsBallInPlayOf(sim));

  // Home's last touch, then the ball crosses the -X goal mouth it now attacks.
  SimulationAccess::TouchesOf(sim).Record(0, 1, e_TouchType_Intentional_Kicked);
  football::ball::Ball* ball = SimulationAccess::BallOf(sim);
  football::ball::BallState state = ball->state();
  state.position = football::math::Vector3(-52.45f, 0.0f, 0.6f);
  state.velocity = football::math::Vector3(-40.0f, 0.0f, 0.0f);
  SimulationAccess::SetBallState(sim, state);
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

TEST_CASE("Runtime harness: the observation is the single Stadium Frame",
          "[runtime][sim]") {
  for (const bool reverse : {false, true}) {
    CAPTURE(reverse);
    Simulation sim;
    Init(sim, reverse, football::sim::Minutes(45));
    SimulationAccess::ResetBall(sim, football::math::Vector3(3.5f, -2.5f, 0.0f));
    for (int half = 0; half < 2; ++half) {
      // The observation publishes the authoritative ball position unchanged.
      const football::math::Vector3 raw =
          SimulationAccess::BallOf(sim)->state().position;
      const WorldState world = sim.Observe();
      CHECK(world.ball_position.coords[0] == raw.coords[0]);
      CHECK(world.ball_position.coords[1] == raw.coords[1]);
      // Team directions are the live Stadium Frame orientations.
      const auto home_view = SimulationAccess::TeamViewOf(sim, 0);
      const auto away_view = SimulationAccess::TeamViewOf(sim, 1);
      CHECK(world.teams[0].defending_direction == home_view.defending_direction);
      CHECK(world.teams[1].defending_direction == away_view.defending_direction);
      CHECK(away_view.defending_direction == -home_view.defending_direction);
      if (half == 0) SimulationAccess::ApplyChangeOfEndsNow(sim);
    }
    CHECK(sim.Stop());
  }
}

// ---------------------------------------------------------------------------
// NEXT-1/NEXT-2: the restart planner and the initial state must follow the
// live defending direction, not the team slot, and processing order must not
// move any input position.
// ---------------------------------------------------------------------------

namespace {

std::vector<Player*> RosterOf(Simulation& sim) {
  std::vector<Player*> players;
  for (int team = 0; team < 2; ++team)
    for (Player* player : SimulationAccess::TeamOf(sim, team).Players())
      players.push_back(player);
  return players;
}

RestartPlanningTable PlanningOf(Simulation& sim) {
  return RestartPlanningTable{
      {SimulationAccess::TeamOrientationOf(sim, 0).defending_direction,
       &SimulationAccess::DefinitionOf(sim, 0).set_piece_orders,
       &SimulationAccess::DefinitionOf(sim, 0).formations, nullptr},
      {SimulationAccess::TeamOrientationOf(sim, 1).defending_direction,
       &SimulationAccess::DefinitionOf(sim, 1).set_piece_orders,
       &SimulationAccess::DefinitionOf(sim, 1).formations, nullptr}};
}

Player* PlayerOf(const std::vector<Player*>& players, football::player::PlayerId id) {
  for (Player* player : players)
    if (player->GetID() == id) return player;
  return nullptr;
}

}  // namespace

TEST_CASE("Runtime harness: processing order changes only the tick order",
          "[runtime][sim]") {
  Simulation normal;
  Simulation reverse;
  MatchOptions n;
  n.snapshot_capacity = 8;
  n.ball_position = football::math::Vector3(3.5f, -2.5f, 0.0f);
  MatchOptions r = n;
  r.reverse_team_processing = true;
  normal.Init(SmallTeam(0), SmallTeam(2), football::pitch::Pitch{}, n,
              football::ball::BallModel{});
  reverse.Init(SmallTeam(0), SmallTeam(2), football::pitch::Pitch{}, r,
               football::ball::BallModel{});

  // reverse_team_processing only reorders the actor loops: the initial ball
  // and both team orientations must be identical.
  CHECK(normal.Observe().ball_position == reverse.Observe().ball_position);
  for (int team = 0; team < 2; ++team) {
    CHECK(SimulationAccess::TeamOrientationOf(normal, team).defending_direction ==
          SimulationAccess::TeamOrientationOf(reverse, team).defending_direction);
    for (Player* player : SimulationAccess::TeamOf(normal, team).Players())
      CHECK(player->GetDefendingDirection() ==
            SimulationAccess::TeamOf(reverse, team)
                .Players()[static_cast<std::size_t>(player->GetID() - team * 2)]
                ->GetDefendingDirection());
  }
  CHECK(normal.Stop());
  CHECK(reverse.Stop());
}

TEST_CASE("Runtime harness: a kickoff plan stands in the half the team defends",
          "[runtime][sim]") {
  for (const bool reverse : {false, true}) {
    CAPTURE(reverse);
    Simulation sim;
    Init(sim, reverse, football::sim::Minutes(45));
    OpenHalf(sim, MatchPhase::FirstHalf);
    const std::vector<Player*> players = RosterOf(sim);

    const auto assert_own_half = [&]() {
      const RestartPlan plan = PlanRestart(
          SimulationAccess::PitchOf(sim), football::math::Vector3(0.0f, 0.0f, 0.0f),
          players, e_GameMode_KickOff, 0, PlanningOf(sim));
      for (const RestartPlayerTarget& target : plan.players) {
        Player* player = PlayerOf(players, target.player);
        REQUIRE(player != nullptr);
        CHECK(target.position.coords[0] * player->GetDefendingDirection() >= -0.01f);
        CHECK(std::fabs(target.position.coords[1]) <=
              SimulationAccess::PitchOf(sim).half_width());
      }
    };

    // First half: Home defends -X.
    REQUIRE(SimulationAccess::TeamOrientationOf(sim, 0).defending_direction == -1);
    assert_own_half();
    // Second half: the same planner call must follow the flipped direction.
    SimulationAccess::ApplyChangeOfEndsNow(sim);
    REQUIRE(SimulationAccess::TeamOrientationOf(sim, 0).defending_direction == 1);
    assert_own_half();
    CHECK(sim.Stop());
  }
}

TEST_CASE("Runtime harness: a penalty goalkeeper stands on the defended goal",
          "[runtime][sim]") {
  for (const bool reverse : {false, true}) {
    CAPTURE(reverse);
    Simulation sim;
    Init(sim, reverse, football::sim::Minutes(45));
    OpenHalf(sim, MatchPhase::FirstHalf);
    const std::vector<Player*> players = RosterOf(sim);
    const football::pitch::Pitch& pitch = SimulationAccess::PitchOf(sim);

    const auto assert_goalkeeper = [&](int half) {
      const int away_side =
          SimulationAccess::TeamOrientationOf(sim, 1).defending_direction;
      const football::math::Vector3 spot(
          away_side * (pitch.half_length() - pitch.penalty_mark_distance()), 0.0f,
          0.0f);
      const RestartPlan plan = PlanRestart(pitch, spot, players, e_GameMode_Penalty, 0,
                                           PlanningOf(sim));
      bool checked = false;
      for (const RestartPlayerTarget& target : plan.players) {
        Player* player = PlayerOf(players, target.player);
        REQUIRE(player != nullptr);
        if (player->GetTeamID() != 1 ||
            player->GetFormationEntry().role != e_PlayerRole_GK) continue;
        CHECK(target.position.coords[0] ==
              Catch::Approx(static_cast<float>(away_side) * pitch.half_length()));
        CHECK(std::fabs(target.position.coords[1]) <=
              pitch.goal_half_width());
        checked = true;
      }
      CHECK(checked);
      CHECK(SimulationAccess::TeamOrientationOf(sim, 0).defending_direction ==
            (half == 0 ? -1 : 1));
    };

    assert_goalkeeper(0);
    SimulationAccess::ApplyChangeOfEndsNow(sim);
    assert_goalkeeper(1);
    CHECK(sim.Stop());
  }
}

TEST_CASE("Runtime harness: a corner keeps its declared left/right taker across halves",
          "[runtime][sim]") {
  TeamSetup home = SmallTeam(0);
  home.set_piece_orders.corners_left = {0};
  home.set_piece_orders.corners_right = {1};
  MatchOptions options;
  options.snapshot_capacity = 8;
  Simulation sim;
  sim.Init(home, SmallTeam(2), football::pitch::Pitch{}, options,
           football::ball::BallModel{});
  OpenHalf(sim, MatchPhase::FirstHalf);
  const std::vector<Player*> players = RosterOf(sim);
  const football::pitch::Pitch& pitch = SimulationAccess::PitchOf(sim);

  const auto corner_taker = [&](const football::math::Vector3& spot) {
    return PlanRestart(pitch, spot, players, e_GameMode_Corner, 0, PlanningOf(sim))
        .taker;
  };

  // First half: Home attacks +X, so its own left corner is at +Y.
  REQUIRE(SimulationAccess::TeamOrientationOf(sim, 0).defending_direction == -1);
  CHECK(corner_taker({pitch.half_length(), pitch.half_width(), 0.0f}) == 0);
  CHECK(corner_taker({pitch.half_length(), -pitch.half_width(), 0.0f}) == 1);
  // Second half: the same physical left corner is now at -Y, and the same
  // declared taker owns it.
  SimulationAccess::ApplyChangeOfEndsNow(sim);
  REQUIRE(SimulationAccess::TeamOrientationOf(sim, 0).defending_direction == 1);
  CHECK(corner_taker({-pitch.half_length(), -pitch.half_width(), 0.0f}) == 0);
  CHECK(corner_taker({-pitch.half_length(), pitch.half_width(), 0.0f}) == 1);
  CHECK(sim.Stop());
}

TEST_CASE("Runtime harness: a goal kick clears the area the team defends",
          "[runtime][sim]") {
  for (const bool reverse : {false, true}) {
    CAPTURE(reverse);
    Simulation sim;
    Init(sim, reverse, football::sim::Minutes(45));
    OpenHalf(sim, MatchPhase::FirstHalf);
    const std::vector<Player*> players = RosterOf(sim);
    const football::pitch::Pitch& pitch = SimulationAccess::PitchOf(sim);

    const auto assert_area_clear = [&]() {
      const int awarded_side =
          SimulationAccess::TeamOrientationOf(sim, 0).defending_direction;
      // Park an opponent inside the area the awarded team defends.
      Player* opponent = nullptr;
      for (Player* player : SimulationAccess::TeamOf(sim, 1).Players())
        if (player->GetFormationEntry().role != e_PlayerRole_GK) opponent = player;
      REQUIRE(opponent != nullptr);
      opponent->ResetPosition(
          football::math::Vector3(awarded_side * (pitch.half_length() - 5.0f), 0.0f,
                                   0.0f),
          football::math::Vector3(0.0f, 0.0f, 0.0f));
      const football::math::Vector3 spot(
          awarded_side * (pitch.half_length() - 5.5f), 0.0f, 0.0f);
      const RestartPlan plan = PlanRestart(pitch, spot, players, e_GameMode_GoalKick,
                                           0, PlanningOf(sim));
      const football::math::Vector3 target =
          [&] {
            for (const RestartPlayerTarget& candidate : plan.players)
              if (candidate.player == opponent->GetID()) return candidate.position;
            return football::math::Vector3();
          }();
      // The opponent is pushed out of the defended penalty area.
      CHECK(target.coords[0] * awarded_side <=
            pitch.half_length() - pitch.penalty_area_depth() + 0.01f);
      CHECK(target != football::math::Vector3());
    };

    assert_area_clear();
    SimulationAccess::ApplyChangeOfEndsNow(sim);
    assert_area_clear();
    CHECK(sim.Stop());
  }
}
