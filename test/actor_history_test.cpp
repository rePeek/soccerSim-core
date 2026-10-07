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
    auto& match = *simulation.match();
    auto* actor = match.GetTeam(match.FirstTeam())->GetAllPlayers()[0]; // Tactical phase zero.
    simulation.AdvanceTime(TickSpan{(10 - match.GetTimelineTick().value % 10) % 10});
    REQUIRE(match.IsInPlay());
    REQUIRE_NOTHROW(simulation.GetMentalImage(TickSpan{}));
    const auto rng = match.rng().engine();
    auto& touch_sink = SimulationAccess::EventsOf(simulation);
    REQUIRE_THROWS_AS(actor->Process(SimulationAccess::PlayerTickOf(simulation), {}, touch_sink), std::logic_error); // No fallback to Match history.
    REQUIRE(match.rng().engine() == rng);

    std::vector<Player*> players;
    match.GetTeam(match.FirstTeam())->GetActivePlayers(players);
    match.GetTeam(match.SecondTeam())->GetActivePlayers(players);
    std::vector<MentalImage> supplied;
    supplied.emplace_back(match.GetTimelineTick(), players, *match.GetBall());
    // Distinct caller-owned observation facts; never mutate live opponent positions.
    supplied[0].maxDistanceDeviation = 1000.0f;
    for (auto& image : supplied[0].players) image.position = actor->GetPosition();
    const auto position = actor->GetPosition();
    const auto forward_focus = position + Vector3(-actor->GetTeam()->GetDynamicSide(), 0, 0) * sprintVelocity * 0.5f;
    const auto forward = query::CalculateFreeSpace(match.GetTimelineTick(), &supplied[0], actor->GetTeamID(), forward_focus, 5.0f, 0.5f);
    const auto space = query::CalculateFreeSpace(match.GetTimelineTick(), &supplied[0], actor->GetTeamID(),
        position + actor->GetMovement() * 0.1f, 5.0f, 0.1f);
    const auto owned_forward = query::CalculateFreeSpace(match.GetTimelineTick(), simulation.GetMentalImage(TickSpan{}),
        actor->GetTeamID(), forward_focus, 5.0f, 0.5f);
    REQUIRE(forward != owned_forward);
    actor->Process(SimulationAccess::PlayerTickOf(simulation), supplied, touch_sink);
    REQUIRE(actor->GetTacticalSituation().forwardSpaceRating == forward);
    REQUIRE(actor->GetTacticalSituation().spaceRating == space);
    REQUIRE(supplied[0].captured_tick == match.GetTimelineTick());
  }
}

TEST_CASE("Humanoid consumes its tick-local span even when Match history is populated", "[sim][history][humanoid]") {
  for (bool reverse : {false, true}) {
    Simulation simulation;
    MatchOptions options; options.reverse_team_processing = reverse;
    simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
        football::app::fixtures::MakeDefaultAwayTeam(), football::model::MakeLegacyPitch(), options);
    simulation.Step({});
    auto& match = *simulation.match();
    REQUIRE_NOTHROW(simulation.GetMentalImage(TickSpan{}));
    const auto rng = match.rng().engine();
    auto* actor = match.GetTeam(match.FirstTeam())->GetAllPlayers()[0];
    const auto position = actor->GetPosition();
    auto& touch_sink = SimulationAccess::EventsOf(simulation);
    REQUIRE_THROWS_AS(actor->CastHumanoid()->Process({}, touch_sink), std::logic_error);
    REQUIRE(actor->GetPosition() == position);
    REQUIRE(match.rng().engine() == rng);
    std::vector<MentalImage> supplied;
    supplied.emplace_back(match.GetTimelineTick(), std::span<Player* const>{}, *match.GetBall());
    REQUIRE_NOTHROW(actor->Process(SimulationAccess::PlayerTickOf(simulation), supplied, touch_sink)); // Ball-only caller-owned sample; no stored borrow.
    REQUIRE(supplied[0].players.empty());
    REQUIRE(supplied[0].captured_tick == match.GetTimelineTick());
  }
}


static_assert(!std::is_constructible_v<Humanoid, Player*>);
static_assert(!std::is_constructible_v<HumanoidBase, Player*, Match*>);
static_assert(!std::is_constructible_v<Player, Team*, const football::model::Player&, std::uint8_t>);

TEST_CASE("Humanoid baked clips come from its injected library, not the runtime owner",
          "[sim][animation][dependency]") {
  Simulation simulation;
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(), football::model::MakeLegacyPitch(), {});
  auto& match = *simulation.match();
  AnimationLibrary independent;
  REQUIRE(independent.Load(std::filesystem::path(__FILE__).parent_path().parent_path() /
      "assets/runtime/animations.simanim"));
  auto* actor = match.GetTeam(0)->GetAllPlayers()[1];
  Humanoid humanoid(actor, independent);
  const auto id = humanoid.GetCurrentAnim()->animationId;
  REQUIRE(&humanoid.GetBakedClip(id) == &independent.Get(static_cast<std::uint32_t>(id)));
  REQUIRE(&humanoid.GetBakedClip(id) != &match.GetAnimationLibrary().Get(static_cast<std::uint32_t>(id)));
  REQUIRE(&actor->CastHumanoid()->GetBakedClip(id) ==
          &match.GetAnimationLibrary().Get(static_cast<std::uint32_t>(id)));
}

TEST_CASE("Ball difficulty uses the supplied ball, touch clock and RNG",
          "[sim][humanoid][dependency]") {
  Simulation simulation;
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(), football::model::MakeLegacyPitch(), {});
  auto& match = *simulation.match();
  auto* actor = match.GetTeam(0)->GetAllPlayers()[1];
  auto& opponent = *match.GetTeam(1);
  auto* toucher = opponent.GetAllPlayers()[1];
  const auto ambient_rng = match.rng().engine();
  Ball supplied(football::model::MakeLegacyPitch());
  SpatialState spatial;
  spatial.position = Vector3(0);
  spatial.directionVec = Vector3(0, -1, 0);
  supplied.SetPosition(Vector3(0, -0.2f, 0.11f), {});
  event::TouchState touches;
  touches.Record(1, toucher->GetID(), e_TouchType_Intentional_Kicked);
  toucher->SetLastTouchTick(Tick{100});
  SimulationRng rng;
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
  REQUIRE(match.rng().engine() == ambient_rng);
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
  auto& match = *simulation.match();
  auto* actor = match.GetTeam(match.FirstTeam())->GetAllPlayers()[0];
  REQUIRE_FALSE(match.IsInPlay());
  REQUIRE(match.GetTimelineTick() == Tick{1});
  std::vector<Player*> players;
  match.GetTeam(0)->GetActivePlayers(players);
  match.GetTeam(1)->GetActivePlayers(players);
  std::vector<MentalImage> history;
  history.emplace_back(Tick{10}, players, *match.GetBall());
  history[0].maxDistanceDeviation = 1000.f;
  for (auto& image : history[0].players) image.position = Vector3(100, 100, 0);
  bool underway = false;
  const PlayerTickContext tick{Tick{10}, true, underway, nullptr};
  const auto position = actor->GetPosition();
  const auto focus = position + Vector3(-actor->GetTeam()->GetDynamicSide(), 0, 0) * sprintVelocity * 0.5f;
  const float expected = query::CalculateFreeSpace(tick.now, &history[0], actor->GetTeamID(), focus, 5.0f, 0.5f);
  REQUIRE(expected == 1.f);
  REQUIRE(actor->GetTacticalSituation().forwardSpaceRating == 0.f);
  actor->Process(tick, history, SimulationAccess::EventsOf(simulation));
  REQUIRE(actor->GetTacticalSituation().forwardSpaceRating == expected);
  REQUIRE(match.GetTimelineTick() == Tick{1});
  REQUIRE_FALSE(match.IsInPlay());
}

TEST_CASE("Player's half-underway input remains live across synchronous clock commands",
          "[sim][player][clock]") {
  Simulation simulation;
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(), football::model::MakeLegacyPitch(), {});
  auto tick = SimulationAccess::PlayerTickOf(simulation);
  REQUIRE_FALSE(tick.half_underway);
  simulation.match()->SetMatchPhase(MatchPhase::FirstHalf);
  simulation.match()->StartPlay();
  simulation.match()->StartBallInPlay();
  REQUIRE(tick.half_underway);
  simulation.match()->EndHalf();
  REQUIRE_FALSE(tick.half_underway);
}
}  // namespace
