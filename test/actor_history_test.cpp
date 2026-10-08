#include <vector>
#include <filesystem>
#include <type_traits>
#include "sim/animation/library.hpp"

#include <catch2/catch_test_macros.hpp>

#include "app/fixtures/default_teams.hpp"
#include "default_ai_fixture.hpp"
#include "sim/observation/mentalimage.hpp"
#include "sim/event/ball_touch_sink.hpp"
#include "sim/query/player_query.hpp"
#include "sim/player/player_motion_constants.hpp"
#include "sim/testing/simulation_access.hpp"
#include "sim/player/humanoid/humanoid_utils.hpp"
using football::sim::testing::SimulationAccess;

namespace {
using namespace football::sim;
using blunted::Vector3;

template<class T> concept HasImplicitTick = requires(T& actor) { actor.Process(); };
static_assert(!HasImplicitTick<Team>);
static_assert(!HasImplicitTick<Player>);
static_assert(!HasImplicitTick<Humanoid>);
static_assert(!HasImplicitTick<HumanoidBase>);

TEST_CASE("Player tactical sampling consumes only the tick-local supplied history", "[sim][history][player]") {
  for (bool reverse : {false, true}) {
    Simulation simulation;
    MatchOptions options; options.reverse_team_processing = reverse;
    simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
        football::app::fixtures::MakeDefaultAwayTeam(), football::model::MakeLegacyPitch(), options);
    football::test::TakeKickOff(simulation);
    auto* actor = SimulationAccess::TeamOf(simulation, SimulationAccess::FirstTeamOf(simulation))->GetAllPlayers()[0]; // Tactical phase zero.
    simulation.AdvanceTime(TickSpan{(10 - SimulationAccess::NowOf(simulation).value % 10) % 10});
    REQUIRE(SimulationAccess::IsInPlayOf(simulation));
    REQUIRE_NOTHROW(simulation.GetMentalImage(TickSpan{}));
    const auto rng = SimulationAccess::RngOf(simulation).engine();
    auto& touch_sink = SimulationAccess::EventsOf(simulation);
    REQUIRE_THROWS_AS(actor->Process(SimulationAccess::PlayerTickOf(simulation, *actor), {}, touch_sink, SimulationAccess::RuntimeOf(simulation)), std::logic_error); // No fallback to Match history.
    REQUIRE(SimulationAccess::RngOf(simulation).engine() == rng);

    std::vector<Player*> players;
    SimulationAccess::TeamOf(simulation, SimulationAccess::FirstTeamOf(simulation))->GetActivePlayers(players);
    SimulationAccess::TeamOf(simulation, SimulationAccess::SecondTeamOf(simulation))->GetActivePlayers(players);
    std::vector<MentalImage> supplied;
    supplied.emplace_back(SimulationAccess::NowOf(simulation), players, *SimulationAccess::BallOf(simulation));
    // Distinct caller-owned observation facts; never mutate live opponent positions.
    supplied[0].maxDistanceDeviation = 1000.0f;
    for (auto& image : supplied[0].players) image.position = actor->GetPosition();
    const auto position = actor->GetPosition();
    const auto forward_focus = position + Vector3(-actor->GetTeam()->GetDynamicSide(), 0, 0) * sprintVelocity * 0.5f;
    const auto forward = query::CalculateFreeSpace(SimulationAccess::NowOf(simulation), &supplied[0], actor->GetTeamID(), forward_focus, 5.0f, 0.5f);
    const auto space = query::CalculateFreeSpace(SimulationAccess::NowOf(simulation), &supplied[0], actor->GetTeamID(),
        position + actor->GetMovement() * 0.1f, 5.0f, 0.1f);
    const auto owned_forward = query::CalculateFreeSpace(SimulationAccess::NowOf(simulation), simulation.GetMentalImage(TickSpan{}),
        actor->GetTeamID(), forward_focus, 5.0f, 0.5f);
    REQUIRE(forward != owned_forward);
    actor->Process(SimulationAccess::PlayerTickOf(simulation, *actor), supplied, touch_sink, SimulationAccess::RuntimeOf(simulation));
    REQUIRE(actor->GetTacticalSituation().forwardSpaceRating == forward);
    REQUIRE(actor->GetTacticalSituation().spaceRating == space);
    REQUIRE(supplied[0].captured_tick == SimulationAccess::NowOf(simulation));
  }
}

TEST_CASE("Humanoid consumes its tick-local span even when Match history is populated", "[sim][history][humanoid]") {
  for (bool reverse : {false, true}) {
    Simulation simulation;
    MatchOptions options; options.reverse_team_processing = reverse;
    simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
        football::app::fixtures::MakeDefaultAwayTeam(), football::model::MakeLegacyPitch(), options);
    simulation.Step({});
    REQUIRE_NOTHROW(simulation.GetMentalImage(TickSpan{}));
    const auto rng = SimulationAccess::RngOf(simulation).engine();
    auto* actor = SimulationAccess::TeamOf(simulation, SimulationAccess::FirstTeamOf(simulation))->GetAllPlayers()[0];
    const auto position = actor->GetPosition();
    auto& touch_sink = SimulationAccess::EventsOf(simulation);
    REQUIRE_THROWS_AS(actor->CastHumanoid()->Process(SimulationAccess::NowOf(simulation), SimulationAccess::PlayerTickOf(simulation, *actor), {}, touch_sink, SimulationAccess::RuntimeOf(simulation)), std::logic_error);
    REQUIRE(actor->GetPosition() == position);
    REQUIRE(SimulationAccess::RngOf(simulation).engine() == rng);
    std::vector<MentalImage> supplied;
    supplied.emplace_back(SimulationAccess::NowOf(simulation), std::span<Player* const>{}, *SimulationAccess::BallOf(simulation));
    REQUIRE_NOTHROW(actor->Process(SimulationAccess::PlayerTickOf(simulation, *actor), supplied, touch_sink, SimulationAccess::RuntimeOf(simulation))); // Ball-only caller-owned sample; no stored borrow.
    REQUIRE(supplied[0].players.empty());
    REQUIRE(supplied[0].captured_tick == SimulationAccess::NowOf(simulation));
  }
}


static_assert(!std::is_constructible_v<Humanoid, Player*>);
static_assert(!std::is_constructible_v<Humanoid, Player*, const AnimationLibrary&>);
static_assert(!std::is_constructible_v<HumanoidBase, Player*>);
static_assert(!std::is_constructible_v<HumanoidBase, Player*, const AnimationLibrary&>);
static_assert(!std::is_constructible_v<Player, Team*, const football::model::Player&, std::uint8_t>);

TEST_CASE("Humanoid baked clips come from its injected library, not the runtime owner",
          "[sim][animation][dependency]") {
  Simulation simulation;
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(), football::model::MakeLegacyPitch(), {});
  AnimationLibrary independent;
  REQUIRE(independent.Load(std::filesystem::path(__FILE__).parent_path().parent_path() /
      "assets/runtime/animations.simanim"));
  auto* actor = SimulationAccess::TeamOf(simulation, 0)->GetAllPlayers()[1];
  blunted::Rng supplied_rng;
  supplied_rng.Seed(99);
  auto expected_rng = supplied_rng;
  const auto ambient_rng = SimulationAccess::RngOf(simulation).engine();
  Humanoid humanoid(actor, independent, supplied_rng);
  const auto id = humanoid.GetCurrentAnim()->animationId;
  REQUIRE(&humanoid.GetBakedClip(id) == &independent.Get(static_cast<std::uint32_t>(id)));
  REQUIRE(&humanoid.GetBakedClip(id) != &SimulationAccess::AnimationLibraryOf(simulation).Get(static_cast<std::uint32_t>(id)));
  REQUIRE(&actor->CastHumanoid()->GetBakedClip(id) ==
          &SimulationAccess::AnimationLibraryOf(simulation).Get(static_cast<std::uint32_t>(id)));
  (void)expected_rng.Uniform(0, static_cast<int>(humanoid.GetBakedClip(id).frame_count) - 2);
  REQUIRE(supplied_rng.engine() == expected_rng.engine());
  humanoid.ResetSituation(actor->GetPosition());
  (void)expected_rng.Uniform(0, static_cast<int>(humanoid.GetBakedClip(humanoid.GetCurrentAnim()->animationId).frame_count) - 2);
  REQUIRE(supplied_rng.engine() == expected_rng.engine());
  REQUIRE(SimulationAccess::RngOf(simulation).engine() == ambient_rng);
}

TEST_CASE("Ball difficulty uses the supplied ball, touch clock and RNG",
          "[sim][humanoid][dependency]") {
  Simulation simulation;
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(), football::model::MakeLegacyPitch(), {});
  auto* actor = SimulationAccess::TeamOf(simulation, 0)->GetAllPlayers()[1];
  auto& opponent = *SimulationAccess::TeamOf(simulation, 1);
  auto* toucher = opponent.GetAllPlayers()[1];
  const auto ambient_rng = SimulationAccess::RngOf(simulation).engine();
  Ball supplied(football::model::MakeLegacyPitch());
  SpatialState spatial;
  spatial.position = Vector3(0);
  spatial.directionVec = Vector3(0, -1, 0);
  supplied.SetPosition(Vector3(0, -0.2f, 0.11f), {});
  event::TouchState touches;
  touches.Record(1, toucher->GetID(), e_TouchType_Intentional_Kicked);
  toucher->SetLastTouchTick(Tick{100});
  blunted::Rng rng;
  rng.Seed(92);
  const auto before = rng.engine();
  float distance, height, movement;
  GetDifficultyFactors(&supplied, actor, touches, opponent, Tick{100}, rng, spatial,
      Vector3(0), distance, height, movement);
  REQUIRE(distance > 0.0f);
  REQUIRE(height > 0.0f);
  rng.engine() = before;
  GetDifficultyFactors(&supplied, actor, touches, opponent, Tick{300}, rng, spatial,
      Vector3(0), distance, height, movement);
  REQUIRE(distance == 0.0f);
  REQUIRE(height == 0.0f);
  REQUIRE(movement == 0.0f);
  auto expected = rng;
  expected.engine() = before;
  (void)expected.Uniform(0.5f, 1.0f);
  REQUIRE(rng.engine() == expected.engine());
  supplied.SetPosition(Vector3(4, 0, 2), {});
  rng.engine() = before;
  GetDifficultyFactors(&supplied, actor, touches, opponent, Tick{300}, rng, spatial,
      Vector3(0), distance, height, movement);
  REQUIRE(distance > 0.0f);
  REQUIRE(height > 0.0f);
  REQUIRE(SimulationAccess::RngOf(simulation).engine() == ambient_rng);
}

template<class T> concept HasImplicitActorFacts = requires(T& actor, std::span<MentalImage> history, BallTouchSink& events) {
  actor.Process(history, events);
};
static_assert(!HasImplicitActorFacts<Player>);

TEST_CASE("Player tactical refresh honors supplied tick and authorization",
          "[sim][player][dependency]") {
  Simulation simulation;
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(), football::model::MakeLegacyPitch(), {});
  simulation.Step({});
  auto* actor = SimulationAccess::TeamOf(simulation, SimulationAccess::FirstTeamOf(simulation))->GetAllPlayers()[0];
  REQUIRE_FALSE(SimulationAccess::IsInPlayOf(simulation));
  REQUIRE(SimulationAccess::NowOf(simulation) == Tick{1});
  std::vector<Player*> players;
  SimulationAccess::TeamOf(simulation, 0)->GetActivePlayers(players);
  SimulationAccess::TeamOf(simulation, 1)->GetActivePlayers(players);
  std::vector<MentalImage> history;
  history.emplace_back(Tick{10}, players, *SimulationAccess::BallOf(simulation));
  history[0].maxDistanceDeviation = 1000.f;
  for (auto& image : history[0].players) image.position = Vector3(100, 100, 0);
  bool underway = false;
  const PlayerTickContext tick{
      Tick{10}, true, false, false, underway, *SimulationAccess::BallOf(simulation), SimulationAccess::BallEnvironmentOf(simulation),
      nullptr, nullptr, nullptr, SimulationAccess::TouchesOf(simulation), SimulationAccess::RefereeOf(simulation)->GetBuffer(),
      SimulationAccess::PitchOf(simulation), *SimulationAccess::TeamOf(simulation, 0), *SimulationAccess::TeamOf(simulation, 1),
      *SimulationAccess::TeamOf(simulation, SimulationAccess::FirstTeamOf(simulation)), *SimulationAccess::TeamOf(simulation, SimulationAccess::SecondTeamOf(simulation)), 0, false,
      SimulationAccess::RngOf(simulation)};
  const auto position = actor->GetPosition();
  const auto focus = position + Vector3(-actor->GetTeam()->GetDynamicSide(), 0, 0) * sprintVelocity * 0.5f;
  const float expected = query::CalculateFreeSpace(tick.now, &history[0], actor->GetTeamID(), focus, 5.0f, 0.5f);
  REQUIRE(expected == 1.f);
  REQUIRE(actor->GetTacticalSituation().forwardSpaceRating == 0.f);
  actor->Process(tick, history, SimulationAccess::EventsOf(simulation), SimulationAccess::RuntimeOf(simulation));
  REQUIRE(actor->GetTacticalSituation().forwardSpaceRating == expected);
  REQUIRE(SimulationAccess::NowOf(simulation) == Tick{1});
  REQUIRE_FALSE(SimulationAccess::IsInPlayOf(simulation));
}

TEST_CASE("Player's half-underway input remains live across synchronous clock commands",
          "[sim][player][clock]") {
  Simulation simulation;
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(), football::model::MakeLegacyPitch(), {});
  auto& first_actor = *SimulationAccess::TeamOf(simulation, 0)->GetAllPlayers()[0];
  auto tick = SimulationAccess::PlayerTickOf(simulation, first_actor);
  REQUIRE(&tick.half_underway == &SimulationAccess::ClockOf(simulation).IsHalfUnderway());
  REQUIRE_FALSE(tick.half_underway);
  SimulationAccess::SetPhase(simulation, MatchPhase::FirstHalf);
  SimulationAccess::StartPlay(simulation);
  SimulationAccess::StartBallInPlay(simulation);
  REQUIRE(tick.half_underway);
  SimulationAccess::EndHalf(simulation);
  REQUIRE_FALSE(tick.half_underway);
}

static_assert(!HasImplicitActorFacts<Humanoid>);
static_assert(!HasImplicitActorFacts<HumanoidBase>);

TEST_CASE("Humanoid scheduling and publication consume the supplied tick",
          "[sim][player][dependency]") {
  Simulation simulation;
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(), football::model::MakeLegacyPitch(), {});
  auto& actor = *SimulationAccess::TeamOf(simulation, 0)->GetAllPlayers()[1];
  std::vector<Player*> players;
  SimulationAccess::TeamOf(simulation, 0)->GetActivePlayers(players);
  SimulationAccess::TeamOf(simulation, 1)->GetActivePlayers(players);
  std::vector<MentalImage> history;
  history.emplace_back(Tick{123}, players, *SimulationAccess::BallOf(simulation));
  actor.CastHumanoid()->Process(Tick{123}, SimulationAccess::PlayerTickOf(simulation, actor), history, SimulationAccess::EventsOf(simulation), SimulationAccess::RuntimeOf(simulation));
  REQUIRE(actor.GetLastDecisionLocomotionPublicationTick() == Tick{123});
  REQUIRE(actor.HasDecisionLocomotionIntent());
  REQUIRE(SimulationAccess::NowOf(simulation) == Tick{});
}
}  // namespace
