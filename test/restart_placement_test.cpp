#include <catch2/catch_test_macros.hpp>

#include "restart_placement_fixture.hpp"
#include "sim/testing/simulation_access.hpp"

TEST_CASE("restart placement retains pre-extraction floats selection and RNG", "[sim][rules]") {
  using namespace football::test;
  // Captured from the original 7dd3c63 TeamAIController::PrepareSetPiece plus
  // formation policy, compiled independently against the same runtime. Compared
  // original/extracted results and RNG after each of 72 cases before recording
  // these three 24-case aggregate fingerprints. No default AI participates.
  constexpr std::array<std::uint64_t, 3> expected{
      UINT64_C(6219363031314221095), UINT64_C(17236150722733869390),
      UINT64_C(1378195650736671099)};
  Simulation simulation;
  for (int roster = 0; roster < 3; ++roster) {
    std::uint64_t hash = UINT64_C(1469598103934665603);
    for (bool reverse : {false, true}) for (int taking_team : {0, 1})
      for (auto mode : restart_modes) {
        RestartCase test{roster, reverse, taking_team, mode};
        SetupRestart(simulation, test);
        const auto takers = PositionRestart(simulation, test);
        REQUIRE(takers[taking_team] != nullptr);
        REQUIRE(takers[1 - taking_team] == nullptr);
        hash = RestartFingerprint(hash, simulation, takers);
      }
    CAPTURE(roster);
    CHECK(hash == expected[roster]);
  }
}
