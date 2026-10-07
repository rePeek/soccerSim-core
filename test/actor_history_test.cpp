#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "app/fixtures/default_teams.hpp"
#include "default_ai_fixture.hpp"
#include "sim/observation/mentalimage.hpp"
#include "sim/match/match_touch_sink.hpp"
#include "sim/query/player_query.hpp"
#include "sim/player/player_motion_constants.hpp"
#include "sim/testing/simulation_access.hpp"
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
    MatchTouchSink touch_sink(match, SimulationAccess::CommandsOf(simulation));
    REQUIRE_THROWS_AS(actor->Process({}, touch_sink), std::logic_error); // No fallback to Match history.
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
    const auto forward = query::CalculateFreeSpace(&match, &supplied[0], actor->GetTeamID(), forward_focus, 5.0f, 0.5f);
    const auto space = query::CalculateFreeSpace(&match, &supplied[0], actor->GetTeamID(),
        position + actor->GetMovement() * 0.1f, 5.0f, 0.1f);
    const auto owned_forward = query::CalculateFreeSpace(&match, simulation.GetMentalImage(TickSpan{}),
        actor->GetTeamID(), forward_focus, 5.0f, 0.5f);
    REQUIRE(forward != owned_forward);
    actor->Process(supplied, touch_sink);
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
    MatchTouchSink touch_sink(match, SimulationAccess::CommandsOf(simulation));
    REQUIRE_THROWS_AS(actor->CastHumanoid()->Process({}, touch_sink), std::logic_error);
    REQUIRE(actor->GetPosition() == position);
    REQUIRE(match.rng().engine() == rng);
    std::vector<MentalImage> supplied;
    supplied.emplace_back(match.GetTimelineTick(), std::span<Player* const>{}, *match.GetBall());
    REQUIRE_NOTHROW(actor->Process(supplied, touch_sink)); // Ball-only caller-owned sample; no stored borrow.
    REQUIRE(supplied[0].players.empty());
    REQUIRE(supplied[0].captured_tick == match.GetTimelineTick());
  }
}

}  // namespace
