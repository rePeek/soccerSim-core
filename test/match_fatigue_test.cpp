#include <catch2/catch_test_macros.hpp>

#include "app/fixtures/default_teams.hpp"
#include "default_ai_fixture.hpp"
#include "sim/match/match_touch_sink.hpp"
#include "sim/testing/simulation_access.hpp"
using football::sim::testing::SimulationAccess;

TEST_CASE("fatigue charges real metres during dead balls but not ceremonial warmup", "[sim][clock][fatigue]") {
  for (bool half_underway : {false, true}) {
    Simulation simulation;
    simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
        football::app::fixtures::MakeDefaultAwayTeam(),
        football::model::MakeLegacyPitch(), MatchOptions{});
    if (half_underway) football::test::TakeKickOff(simulation);
    else simulation.Step({}); // Establish the native history before directly processing an actor.
    auto& match = *simulation.match();
    match.StopPlay(); // Dead ball, not EndHalf: ordinary stoppages still belong to the half.
    REQUIRE(match.IsHalfUnderway() == half_underway);
    REQUIRE_FALSE(match.IsBallInPlay());
    auto& actor = *match.GetTeam(0)->GetAllPlayers()[1];
    actor.ResetPosition(blunted::Vector3(-20, -28, 0), blunted::Vector3(-10, -28, 0));
    PlayerControl move;
    move.move_direction = blunted::Vector3(1, 0, 0);
    move.desired_speed = 7.f;
    const float initial_fatigue = actor.GetFatigueFactorInv();
    float metres = 0;
    std::vector<MentalImage> history{*simulation.GetMentalImage(football::sim::TickSpan{}),
        *simulation.GetMentalImage(football::sim::TickSpan{10}),
        *simulation.GetMentalImage(football::sim::TickSpan{20})};
    football::sim::MatchTouchSink touch_sink(*simulation.match(), SimulationAccess::CommandsOf(simulation));
    // Exercise this owner's native motion/update, without collisions or rule resets
    // contaminating the distance. No implicit policy, scale override or fake contact.
    for (int tick = 0; tick < 100; ++tick) {
      const auto before = actor.GetPosition();
      const float fatigue = actor.GetFatigueFactorInv();
      const float stamina = actor.GetStaminaStat();
      actor.SetControl(move);
      actor.Process(history, touch_sink);
      REQUIRE(actor.GetSimulationActionState().IsPureLocomotion(false));
      const float distance = (actor.GetPosition() - before).GetLength();
      metres += distance;
      const float expected = half_underway
          ? fatigue - distance * 0.00003f * (2.f - stamina) : fatigue;
      REQUIRE(actor.GetFatigueFactorInv() == expected);
      simulation.AdvanceTime(football::sim::TickSpan{1});
    }
    REQUIRE(metres > 1.f);
    if (half_underway) REQUIRE(actor.GetFatigueFactorInv() < initial_fatigue);
    else REQUIRE(actor.GetFatigueFactorInv() == initial_fatigue);
  }
}
