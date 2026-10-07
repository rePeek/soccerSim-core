#include <catch2/catch_test_macros.hpp>

#include "app/fixtures/default_teams.hpp"
#include "default_ai_fixture.hpp"
#include "sim/match/match.hpp"
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
      football::model::MakeLegacyPitch(), options);
  football::test::TakeKickOff(simulation);
  for (int i = 0; i < 40; ++i) simulation.Step({});
}
void ProcessRules(Simulation& simulation) {
  auto& match = *simulation.match();
  const bool reverse = match.options().reverse_team_processing;
  simulation.Mirror(reverse, !reverse, false);
  SimulationAccess::ProcessRules(simulation, *match.GetReferee());
  simulation.Mirror(reverse, !reverse, false);
}
void SetBallHome(Match& match, Vector3 position) {
  // Between ticks the ball shares the first processing roster's frame.
  match.GetBall()->ResetSituation(FromHomePitchFrame(*match.GetTeam(match.FirstTeam())).Position(position));
}
void TriggerThrow(Simulation& simulation) {
  Match& match = *simulation.match();
  SetBallHome(match, Vector3(10, 40, 0));
  simulation.Step({});
  REQUIRE(match.GetReferee()->GetBuffer().restart.has_value());
  simulation.Step({});
  REQUIRE(simulation.Observe().restart_pending);
}

TEST_CASE("restart plans are pure and legal through both processing orders and changes of ends",
          "[sim][restart][readiness]") {
  for (bool reverse : {false, true}) {
    Simulation simulation;
    Init(simulation, reverse);
    Match& match = *simulation.match();
    for (bool switched : {false, true}) {
      if (switched) { match.RequestChangeOfEnds(); simulation.Step({}); }
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
          simulation.ResetSituation(ToHomePitchFrame(match).Position(focus));
          const auto rng = match.rng().engine();
          const auto before = simulation.Observe();
          std::vector<Player*> active;
          match.GetTeam(0)->GetActivePlayers(active);
          match.GetTeam(1)->GetActivePlayers(active);
          const auto ball = ToHomePitchFrame(match).Position(match.GetBall()->Predict(TickSpan{}));
          const auto plan = PlanRestart(match.pitch(), ball, active, mode, *match.GetTeam(taking_team));
          const auto again = PlanRestart(match.pitch(), ball, active, mode, *match.GetTeam(taking_team));
          REQUIRE(match.rng().engine() == rng);
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
          REQUIRE(RestartPlayersReady(plan, match.pitch()));
          simulation.Mirror(reverse, !reverse, false);
          REQUIRE(RestartPlayersReady(plan, match.pitch())); // Predicate is not tied to a transient mirror.
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
          REQUIRE_FALSE(RestartPlayersReady(plan, match.pitch()));
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
    Match& match = *simulation.match();
    const auto state = *match.GetReferee()->GetBuffer().restart;
    const auto resets = match.GetResetSequence();
    PlaceRestartPlayersAtTimeout(state.plan); // Test arranges early readiness, not a rule timeout.
    REQUIRE(RestartPlayersReady(state.plan, match.pitch()));
    PlayerControl shot;
    shot.action = ControlAction::Shoot;
    REQUIRE(BuildPlayerCommands(shot, *state.plan.taker, SimulationAccess::CommandInputsOf(simulation)).size() == 1);
    REQUIRE(BuildPlayerCommands(shot, *state.plan.taker, SimulationAccess::CommandInputsOf(simulation))[0].desiredFunctionType == e_FunctionType_Movement);
    simulation.AdvanceTime((state.earliest_restart_tick - match.GetTimelineTick()) - TickSpan{1});
    ProcessRules(simulation);
    REQUIRE_FALSE(match.IsInPlay());
    simulation.AdvanceTime(TickSpan{1});
    SetBallHome(match, state.plan.ball_position + Vector3(0.1f, 0, 0));
    ProcessRules(simulation);
    REQUIRE_FALSE(match.IsInPlay());
    SetBallHome(match, state.plan.ball_position);
    simulation.TouchBall(Vector3(1, 0, 0));
    ProcessRules(simulation);
    REQUIRE_FALSE(match.IsInPlay());
    SetBallHome(match, state.plan.ball_position);
    auto* opponent = match.GetTeam(1 - state.plan.team->GetID())->GetAllPlayers()[1];
    const auto frame = FromHomePitchFrame(*opponent->GetTeam());
    opponent->ResetPosition(frame.Position(state.plan.ball_position), frame.Position(state.plan.ball_position));
    ProcessRules(simulation);
    REQUIRE_FALSE(match.IsInPlay());
    PlaceRestartPlayersAtTimeout(state.plan);
    ProcessRules(simulation);
    CAPTURE(reverse, RestartPlayersReady(state.plan, match.pitch()), simulation.Observe().ball_position.coords[0],
            simulation.Observe().ball_position.coords[1], simulation.Observe().ball_position.coords[2],
            match.GetBall()->GetMovement().GetLength(), match.GetTimelineTick().value, state.earliest_restart_tick.value);
    REQUIRE(match.IsInPlay());
    REQUIRE(match.IsInSetPiece());
    REQUIRE_FALSE(simulation.Observe().restart_pending);
    REQUIRE(match.GetReferee()->GetBuffer().restart->phase == RestartPhase::Ready);
    REQUIRE_FALSE(match.GetReferee()->GetBuffer().restart->used_timeout_placement);
    REQUIRE(match.GetResetSequence() == resets);
    const auto rng = match.rng().engine();
    ProcessRules(simulation);
    REQUIRE(match.rng().engine() == rng);
    // A notification without a scheduled release cannot invent RestartTaken.
    auto& touch_sink = SimulationAccess::EventsOf(simulation);
    touch_sink.OnBallTouched({match.GetTimelineTick(), state.plan.taker, state.plan.team, e_TouchType_Intentional_Nonkicked});
    REQUIRE(match.GetReferee()->GetBuffer().restart->phase == RestartPhase::Ready);
    simulation.AdvanceTime(Seconds(1));
    ProcessRules(simulation);
    REQUIRE(match.IsInSetPiece());
    const auto policy = football::test::MakeDefaultAI(simulation);
    int steps = 0;
    while (match.GetReferee()->GetBuffer().restart->phase == RestartPhase::Ready) {
      football::test::StepDefaultAI(simulation, policy);
      REQUIRE(++steps < 1000);
    }
    REQUIRE(match.GetReferee()->GetBuffer().restart->phase == RestartPhase::Taken);
    REQUIRE(match.GetBall()->GetMovement().GetLength() > 0.5f);
    REQUIRE(match.IsInSetPiece());
    football::test::StepDefaultAI(simulation, policy);
    REQUIRE(match.GetReferee()->GetBuffer().restart->phase == RestartPhase::InPlay);
    REQUIRE_FALSE(match.GetReferee()->GetBuffer().active);
    REQUIRE_FALSE(match.IsInSetPiece());
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
      Match& match = *simulation.match();
      const auto state = *match.GetReferee()->GetBuffer().restart;
      PlaceRestartPlayersAtTimeout(state.plan);
      for (const auto& entry : state.plan.players) {
        if (entry.player != state.plan.taker) continue;
        const auto frame = FromHomePitchFrame(*entry.player->GetTeam());
        entry.player->ResetPosition(frame.Position(entry.position + Vector3(layout == 0 ? 5.f : 25.f, 0, 0)),
                                   frame.Position(state.plan.ball_position));
      }
      const auto policy = football::test::MakeDefaultAI(simulation);
      auto previous_tick = match.GetTimelineTick();
      const auto previous_position = state.plan.taker->GetPosition();
      int steps = 0;
      while (simulation.Observe().restart_pending) {
        football::test::StepDefaultAI(simulation, policy);
        REQUIRE(match.GetTimelineTick() == previous_tick + TickSpan{1});
        previous_tick = match.GetTimelineTick();
        REQUIRE(++steps < 2000);
      }
      REQUIRE(state.plan.taker->GetPosition() != previous_position);
      CAPTURE(reverse, layout, steps);
      REQUIRE_FALSE(match.GetReferee()->GetBuffer().restart->used_timeout_placement);
      const auto authorized = match.GetReferee()->GetBuffer().start_tick;
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
        Match& match = *simulation.match();
        if (switched) { match.RequestChangeOfEnds(); simulation.Step({}); }
        const int last_team = mode == e_GameMode_Corner ? 1 : 0;
    auto& touch_sink = SimulationAccess::EventsOf(simulation);
    touch_sink.OnBallTouched({match.GetTimelineTick(), match.GetTeam(last_team)->GetAllPlayers()[1],
        match.GetTeam(last_team), e_TouchType_Accidental});
        const Vector3 out = mode == e_GameMode_ThrowIn ? Vector3(10, 40, 0) : Vector3(56, 10, 0);
        SetBallHome(match, out);
        const auto tick = match.GetTimelineTick();
        simulation.Step({});
        REQUIRE(match.GetTimelineTick() == tick + TickSpan{1});
        REQUIRE(simulation.Observe().restart == mode);
        simulation.Step({});
        const auto plan = match.GetReferee()->GetBuffer().restart->plan;
        const Vector3 expected = mode == e_GameMode_ThrowIn ? Vector3(10, 36, 0) :
            mode == e_GameMode_Corner ? Vector3(55, 36, 0) : Vector3(50.6f, 0, 0);
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
        auto* old_left = left.match()->GetReferee()->GetBuffer().taker;
        auto* old_right = right.match()->GetReferee()->GetBuffer().taker;
        old_left->SendOff(); old_right->SendOff();
        football::test::StepDefaultAI(left, left_policy);
        football::test::StepDefaultAI(right, right_policy);
        REQUIRE(left.match()->GetReferee()->GetBuffer().taker != old_left);
        REQUIRE(right.match()->GetReferee()->GetBuffer().taker != old_right);
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
      REQUIRE(left.match()->rng().engine() == right.match()->rng().engine());
    }
    REQUIRE(replaced);
  }
}

TEST_CASE("restart planning consumes the supplied ball and roster, not runtime lookups",
          "[sim][restart][inputs]") {
  Simulation simulation;
  Init(simulation, false);
  auto& match = *simulation.match();
  std::vector<Player*> players;
  match.GetTeam(0)->GetActivePlayers(players); // Deliberately omit the opponent roster.
  const auto before = match.GetBall()->Predict(TickSpan{});
  const auto rng = match.rng().engine();
  const Vector3 supplied(12, 6, 0);
  const auto plan = PlanRestart(match.pitch(), supplied, players, e_GameMode_FreeKick,
                                *match.GetTeam(0));
  REQUIRE(plan.ball_position == supplied);
  REQUIRE(plan.players.size() == players.size());
  REQUIRE(plan.taker != nullptr);
  for (const auto& target : plan.players) REQUIRE(target.player->GetTeamID() == 0);
  REQUIRE(match.GetBall()->Predict(TickSpan{}) == before);
  REQUIRE(match.rng().engine() == rng);
}
} // namespace
