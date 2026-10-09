#include <catch2/catch_test_macros.hpp>

#include "app/fixtures/default_teams.hpp"
#include "default_ai_fixture.hpp"
#include "sim/observation/pitch_frame.hpp"
#include "sim/player/player_control_builder.hpp"
#include "sim/rules/restart_readiness.hpp"
#include "sim/testing/simulation_access.hpp"
using football::sim::testing::SimulationAccess;

namespace {
using namespace football::sim;
using blunted::Vector3;

void Init(Simulation& simulation, bool reverse) {
  MatchOptions options;
  options.reverse_team_processing = reverse;
  options.half_duration = Minutes(3);
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(),
      football::model::Pitch{}, options);
  football::test::TakeKickOff(simulation);
  for (int i = 0; i < 40; ++i) simulation.Step({});
}
void ProcessRules(Simulation& simulation) {
  const bool reverse = SimulationAccess::OptionsOf(simulation).reverse_team_processing;
  simulation.Mirror(reverse, !reverse, false);
  SimulationAccess::ProcessRules(simulation, *SimulationAccess::RefereeOf(simulation));
  simulation.Mirror(reverse, !reverse, false);
}
void SetBallHome(Simulation& simulation, Vector3 position) {
  // Between ticks the ball shares the first processing roster's frame.
  SimulationAccess::BallOf(simulation)->ResetSituation(FromHomePitchFrame(*SimulationAccess::TeamOf(simulation, SimulationAccess::FirstTeamOf(simulation))).Position(position));
}
void TriggerThrow(Simulation& simulation) {
  SetBallHome(simulation, Vector3(10, 40, 0));
  simulation.Step({});
  REQUIRE(SimulationAccess::RefereeOf(simulation)->GetBuffer().restart.has_value());
  simulation.Step({});
  REQUIRE(simulation.Observe().restart_pending);
}

TEST_CASE("restart plans are pure and legal through both processing orders and changes of ends",
          "[sim][restart][readiness]") {
  for (bool reverse : {false, true}) {
    Simulation simulation;
    Init(simulation, reverse);
    for (bool switched : {false, true}) {
      if (switched) { SimulationAccess::RequestChangeOfEnds(simulation); simulation.Step({}); }
      for (int taking_team : {0, 1}) {
        const int side = taking_team == 0 ? -1 : 1;
        for (auto mode : {e_GameMode_KickOff, e_GameMode_ThrowIn, e_GameMode_GoalKick,
                          e_GameMode_Corner, e_GameMode_FreeKick, e_GameMode_Penalty}) {
          CAPTURE(reverse, switched, taking_team, mode);
          const Vector3 focus = mode == e_GameMode_ThrowIn ? Vector3(10, 36, 0) :
              mode == e_GameMode_GoalKick ? Vector3(side * 50, 0, 0) :
              mode == e_GameMode_Corner ? Vector3(-side * 55, 36, 0) :
              mode == e_GameMode_Penalty ? Vector3(-side * 44, 0, 0) : Vector3(0);
          simulation.Mirror(reverse, !reverse, false);
          simulation.ResetSituation(ToHomePitchFrame(*SimulationAccess::TeamOf(simulation, reverse ? 1 : 0)).Position(focus));
          const auto rng = SimulationAccess::RngOf(simulation).engine();
          const auto before = simulation.Observe();
          std::vector<Player*> active;
          SimulationAccess::TeamOf(simulation, 0)->GetActivePlayers(active);
          SimulationAccess::TeamOf(simulation, 1)->GetActivePlayers(active);
          const auto ball = ToHomePitchFrame(*SimulationAccess::TeamOf(simulation, reverse ? 1 : 0)).Position(SimulationAccess::BallOf(simulation)->Predict(TickSpan{}));
          const auto plan = PlanRestart(SimulationAccess::PitchOf(simulation), ball, active, mode, *SimulationAccess::TeamOf(simulation, taking_team));
          const auto again = PlanRestart(SimulationAccess::PitchOf(simulation), ball, active, mode, *SimulationAccess::TeamOf(simulation, taking_team));
          REQUIRE(SimulationAccess::RngOf(simulation).engine() == rng);
          REQUIRE(plan.taker != nullptr);
          REQUIRE(plan.taker->GetTeam()->GetID() == taking_team);
          REQUIRE(plan.ball_position.GetDistance(focus) < 0.001f);
          REQUIRE(plan.players.size() == 22);
          REQUIRE(plan.taker == again.taker);
          const auto after = simulation.Observe();
          for (std::size_t i = 0; i < before.players.size(); ++i) {
            REQUIRE(before.players[i].position == after.players[i].position);
            REQUIRE(before.players[i].velocity == after.players[i].velocity);
            REQUIRE(plan.players[i].position == again.players[i].position);
          }
          PlaceRestartPlayersAtTimeout(plan);
          REQUIRE(RestartPlayersReady(plan, SimulationAccess::PitchOf(simulation)));
          simulation.Mirror(reverse, !reverse, false);
          REQUIRE(RestartPlayersReady(plan, SimulationAccess::PitchOf(simulation))); // Predicate is not tied to a transient mirror.
          for (const auto& target : plan.players) {
            const auto observed = ToHomePitchFrame(*target.player->GetTeam()).Position(target.player->GetPosition());
            REQUIRE(observed.GetDistance(target.position) < 0.001f);
            if (target.player == plan.taker) continue;
            if (mode == e_GameMode_KickOff)
              REQUIRE(observed.coords[0] * (target.player->GetTeam()->GetID() == 0 ? -1 : 1) >= 0);
            if (target.player->GetTeam()->GetID() != taking_team &&
                (mode == e_GameMode_FreeKick || mode == e_GameMode_Corner || mode == e_GameMode_KickOff))
              REQUIRE(observed.GetDistance(focus) >= 9.15f);
          }
          auto* taker = plan.taker;
          const auto frame = FromHomePitchFrame(*taker->GetTeam());
          auto target = plan.players.front().position;
          for (const auto& entry : plan.players) if (entry.player == taker) target = entry.position;
          taker->ResetPosition(frame.Position(target + Vector3(1, 0, 0)), frame.Position(focus));
          REQUIRE_FALSE(RestartPlayersReady(plan, SimulationAccess::PitchOf(simulation)));
        }
      }
    }
  }
}

TEST_CASE("restart authorization needs minimum time, legal actors and a placed stationary ball",
          "[sim][restart][readiness][tick]") {
  for (bool reverse : {false, true}) {
    Simulation simulation;
    Init(simulation, reverse);
    TriggerThrow(simulation);
    const auto state = *SimulationAccess::RefereeOf(simulation)->GetBuffer().restart;
    const auto resets = SimulationAccess::ResetSequenceOf(simulation);
    PlaceRestartPlayersAtTimeout(state.plan); // Test arranges early readiness, not a rule timeout.
    REQUIRE(RestartPlayersReady(state.plan, SimulationAccess::PitchOf(simulation)));
    PlayerControl shot;
    shot.action = ControlAction::Shoot;
    REQUIRE(BuildPlayerCommands(shot, *state.plan.taker, SimulationAccess::CommandInputsOf(simulation)).size() == 1);
    REQUIRE(BuildPlayerCommands(shot, *state.plan.taker, SimulationAccess::CommandInputsOf(simulation))[0].desiredFunctionType == e_FunctionType_Movement);
    simulation.AdvanceTime((state.earliest_restart_tick - SimulationAccess::NowOf(simulation)) - TickSpan{1});
    ProcessRules(simulation);
    REQUIRE_FALSE(SimulationAccess::IsInPlayOf(simulation));
    simulation.AdvanceTime(TickSpan{1});
    SetBallHome(simulation, state.plan.ball_position + Vector3(0.1f, 0, 0));
    ProcessRules(simulation);
    REQUIRE_FALSE(SimulationAccess::IsInPlayOf(simulation));
    SetBallHome(simulation, state.plan.ball_position);
    simulation.TouchBall(Vector3(1, 0, 0));
    ProcessRules(simulation);
    REQUIRE_FALSE(SimulationAccess::IsInPlayOf(simulation));
    SetBallHome(simulation, state.plan.ball_position);
    auto* opponent = SimulationAccess::TeamOf(simulation, 1 - state.plan.team->GetID())->GetAllPlayers()[1];
    const auto frame = FromHomePitchFrame(*opponent->GetTeam());
    opponent->ResetPosition(frame.Position(state.plan.ball_position), frame.Position(state.plan.ball_position));
    ProcessRules(simulation);
    REQUIRE_FALSE(SimulationAccess::IsInPlayOf(simulation));
    PlaceRestartPlayersAtTimeout(state.plan);
    ProcessRules(simulation);
    CAPTURE(reverse, RestartPlayersReady(state.plan, SimulationAccess::PitchOf(simulation)), simulation.Observe().ball_position.coords[0],
            simulation.Observe().ball_position.coords[1], simulation.Observe().ball_position.coords[2],
            SimulationAccess::BallOf(simulation)->GetMovement().GetLength(), SimulationAccess::NowOf(simulation).value, state.earliest_restart_tick.value);
    REQUIRE(SimulationAccess::IsInPlayOf(simulation));
    REQUIRE(SimulationAccess::IsInSetPieceOf(simulation));
    REQUIRE_FALSE(simulation.Observe().restart_pending);
    REQUIRE(SimulationAccess::RefereeOf(simulation)->GetBuffer().restart->phase == RestartPhase::Ready);
    REQUIRE_FALSE(SimulationAccess::RefereeOf(simulation)->GetBuffer().restart->used_timeout_placement);
    REQUIRE(SimulationAccess::ResetSequenceOf(simulation) == resets);
    const auto rng = SimulationAccess::RngOf(simulation).engine();
    ProcessRules(simulation);
    REQUIRE(SimulationAccess::RngOf(simulation).engine() == rng);
    // A notification without a scheduled release cannot invent RestartTaken.
    auto& touch_sink = SimulationAccess::EventsOf(simulation);
    touch_sink.OnBallTouched({SimulationAccess::NowOf(simulation), state.plan.taker, state.plan.team, e_TouchType_Intentional_Nonkicked});
    REQUIRE(SimulationAccess::RefereeOf(simulation)->GetBuffer().restart->phase == RestartPhase::Ready);
    simulation.AdvanceTime(Seconds(1));
    ProcessRules(simulation);
    REQUIRE(SimulationAccess::IsInSetPieceOf(simulation));
    const auto policy = football::test::MakeDefaultAI(simulation);
    int steps = 0;
    while (SimulationAccess::RefereeOf(simulation)->GetBuffer().restart->phase == RestartPhase::Ready) {
      football::test::StepDefaultAI(simulation, policy);
      REQUIRE(++steps < 1000);
    }
    REQUIRE(SimulationAccess::RefereeOf(simulation)->GetBuffer().restart->phase == RestartPhase::Taken);
    REQUIRE(SimulationAccess::BallOf(simulation)->GetMovement().GetLength() > 0.5f);
    REQUIRE(SimulationAccess::IsInSetPieceOf(simulation));
    football::test::StepDefaultAI(simulation, policy);
    REQUIRE(SimulationAccess::RefereeOf(simulation)->GetBuffer().restart->phase == RestartPhase::InPlay);
    REQUIRE_FALSE(SimulationAccess::RefereeOf(simulation)->GetBuffer().active);
    REQUIRE_FALSE(SimulationAccess::IsInSetPieceOf(simulation));
  }
}

TEST_CASE("restart positioning executes physics and produces condition-dependent waiting",
          "[sim][restart][readiness]") {
  for (bool reverse : {false, true}) {
    TickSpan waits[2];
    for (int layout : {0, 1}) {
      Simulation simulation;
      Init(simulation, reverse);
      TriggerThrow(simulation);
      const auto state = *SimulationAccess::RefereeOf(simulation)->GetBuffer().restart;
      PlaceRestartPlayersAtTimeout(state.plan);
      for (const auto& entry : state.plan.players) {
        if (entry.player != state.plan.taker) continue;
        const auto frame = FromHomePitchFrame(*entry.player->GetTeam());
        entry.player->ResetPosition(frame.Position(entry.position + Vector3(layout == 0 ? 5.f : 25.f, 0, 0)),
                                   frame.Position(state.plan.ball_position));
      }
      const auto policy = football::test::MakeDefaultAI(simulation);
      auto previous_tick = SimulationAccess::NowOf(simulation);
      const auto previous_position = state.plan.taker->GetPosition();
      int steps = 0;
      while (simulation.Observe().restart_pending) {
        football::test::StepDefaultAI(simulation, policy);
        REQUIRE(SimulationAccess::NowOf(simulation) == previous_tick + TickSpan{1});
        previous_tick = SimulationAccess::NowOf(simulation);
        REQUIRE(++steps < 2000);
      }
      REQUIRE(state.plan.taker->GetPosition() != previous_position);
      CAPTURE(reverse, layout, steps);
      REQUIRE_FALSE(SimulationAccess::RefereeOf(simulation)->GetBuffer().restart->used_timeout_placement);
      const auto authorized = SimulationAccess::RefereeOf(simulation)->GetBuffer().start_tick;
      REQUIRE(authorized >= state.earliest_restart_tick);
      REQUIRE(authorized < state.timeout_tick);
      waits[layout] = authorized - state.entered_tick;
    }
    REQUIRE(waits[1] > waits[0]);
  }
}

TEST_CASE("ordinary ball-out plans retain the right sideline and team in all runtime frames",
          "[sim][restart][readiness][frame]") {
  for (bool reverse : {false, true}) {
    for (bool switched : {false, true}) {
      for (auto mode : {e_GameMode_Corner, e_GameMode_GoalKick, e_GameMode_ThrowIn}) {
        CAPTURE(reverse, switched, mode);
        Simulation simulation;
        Init(simulation, reverse);
        if (switched) { SimulationAccess::RequestChangeOfEnds(simulation); simulation.Step({}); }
        const int last_team = mode == e_GameMode_Corner ? 1 : 0;
    auto& touch_sink = SimulationAccess::EventsOf(simulation);
    touch_sink.OnBallTouched({SimulationAccess::NowOf(simulation), SimulationAccess::TeamOf(simulation, last_team)->GetAllPlayers()[1],
        SimulationAccess::TeamOf(simulation, last_team), e_TouchType_Accidental});
        const Vector3 out = mode == e_GameMode_ThrowIn ? Vector3(10, 40, 0) : Vector3(56, 10, 0);
        SetBallHome(simulation, out);
        const auto tick = SimulationAccess::NowOf(simulation);
        simulation.Step({});
        REQUIRE(SimulationAccess::NowOf(simulation) == tick + TickSpan{1});
        REQUIRE(simulation.Observe().restart == mode);
        simulation.Step({});
        const auto plan = SimulationAccess::RefereeOf(simulation)->GetBuffer().restart->plan;
        const Vector3 expected = mode == e_GameMode_ThrowIn ? Vector3(10, 34, 0) :
            mode == e_GameMode_Corner ? Vector3(52.5f, 34, 0) : Vector3(48.3f, 0, 0);
        REQUIRE(plan.ball_position.GetDistance(expected) < 0.001f);
        REQUIRE(plan.team->GetID() == 1 - last_team);
        REQUIRE(simulation.Observe().ball_position.Get2D().GetDistance(expected) < 0.001f);
        REQUIRE(simulation.Observe().restart_pending);
      }
    }
  }
}

TEST_CASE("pending restart targets, taker replacement and RNG replay as owning values",
          "[sim][restart][replay]") {
  for (bool reverse : {false, true}) {
    Simulation left, right;
    Init(left, reverse); Init(right, reverse);
    TriggerThrow(left); TriggerThrow(right);
    const auto left_policy = football::test::MakeDefaultAI(left);
    const auto right_policy = football::test::MakeDefaultAI(right);
    bool replaced = false;
    for (int step = 0; step < 800; ++step) {
      if (step == 100) {
        REQUIRE(left.Observe().restart_pending);
        auto* old_left = SimulationAccess::RefereeOf(left)->GetBuffer().taker;
        auto* old_right = SimulationAccess::RefereeOf(right)->GetBuffer().taker;
        SimulationAccess::SendOff(left, *old_left); SimulationAccess::SendOff(right, *old_right);
        football::test::StepDefaultAI(left, left_policy);
        football::test::StepDefaultAI(right, right_policy);
        REQUIRE(SimulationAccess::RefereeOf(left)->GetBuffer().taker != old_left);
        REQUIRE(SimulationAccess::RefereeOf(right)->GetBuffer().taker != old_right);
        replaced = true;
      } else {
        football::test::StepDefaultAI(left, left_policy);
        football::test::StepDefaultAI(right, right_policy);
      }
      const auto a = left.Observe(), b = right.Observe();
      REQUIRE(a.tick == b.tick);
      REQUIRE(a.phase == b.phase);
      REQUIRE(a.regulation_time == b.regulation_time);
      REQUIRE(a.ball_in_play_time == b.ball_in_play_time);
      REQUIRE(a.half_underway == b.half_underway);
      REQUIRE(a.ball_in_play == b.ball_in_play);
      REQUIRE(a.reset_sequence == b.reset_sequence);
      REQUIRE(a.in_play == b.in_play);
      REQUIRE(a.in_set_piece == b.in_set_piece);
      REQUIRE(a.restart == b.restart);
      REQUIRE(a.restart_pending == b.restart_pending);
      REQUIRE(a.restart_taker == b.restart_taker);
      REQUIRE(a.ball_retainer == b.ball_retainer);
      REQUIRE(a.pitch == b.pitch);
      REQUIRE(a.ball_position == b.ball_position);
      REQUIRE(a.ball_velocity == b.ball_velocity);
      REQUIRE(a.players.size() == b.players.size());
      for (int side : {0, 1}) {
        REQUIRE(a.teams[side].score == b.teams[side].score);
        REQUIRE(a.teams[side].side == b.teams[side].side);
        REQUIRE(a.teams[side].defending_direction == b.teams[side].defending_direction);
      }
      for (std::size_t i = 0; i < a.players.size(); ++i) {
        const auto& x = a.players[i]; const auto& y = b.players[i];
        REQUIRE(x.id == y.id); REQUIRE(x.side == y.side);
        REQUIRE(x.active == y.active); REQUIRE(x.has_possession == y.has_possession);
        REQUIRE(x.lazy == y.lazy); REQUIRE(x.max_speed == y.max_speed);
        REQUIRE(x.position == y.position); REQUIRE(x.velocity == y.velocity);
        REQUIRE(x.facing == y.facing);
        REQUIRE(x.restart_target == y.restart_target);
      }
      REQUIRE(SimulationAccess::RngOf(left).engine() == SimulationAccess::RngOf(right).engine());
    }
    REQUIRE(replaced);
  }
}

TEST_CASE("restart planning consumes the supplied ball and roster, not runtime lookups",
          "[sim][restart][inputs]") {
  Simulation simulation;
  Init(simulation, false);
  std::vector<Player*> players;
  SimulationAccess::TeamOf(simulation, 0)->GetActivePlayers(players); // Deliberately omit the opponent roster.
  const auto before = SimulationAccess::BallOf(simulation)->Predict(TickSpan{});
  const auto rng = SimulationAccess::RngOf(simulation).engine();
  const Vector3 supplied(12, 6, 0);
  const auto plan = PlanRestart(SimulationAccess::PitchOf(simulation), supplied, players, e_GameMode_FreeKick,
                                *SimulationAccess::TeamOf(simulation, 0));
  REQUIRE(plan.ball_position == supplied);
  REQUIRE(plan.players.size() == players.size());
  REQUIRE(plan.taker != nullptr);
  for (const auto& target : plan.players) REQUIRE(target.player->GetTeamID() == 0);
  REQUIRE(SimulationAccess::BallOf(simulation)->Predict(TickSpan{}) == before);
  REQUIRE(SimulationAccess::RngOf(simulation).engine() == rng);
}
} // namespace
