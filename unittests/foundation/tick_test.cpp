// Tick: exact 100 Hz integer time contract and its failure boundaries.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <stdexcept>

#include "football/foundation/time/tick.hpp"
#include "football/foundation/time/tick_boundary.hpp"

using namespace football::sim;
using Catch::Approx;

TEST_CASE("Tick quantum is 100 Hz", "[unit][foundation][tick]") {
  CHECK(kTicksPerSecond == 100);
  CHECK(kMillisecondsPerTick == 10);
}

TEST_CASE("Tick conversion is exact", "[unit][foundation][tick]") {
  CHECK(Seconds(1).value == 100);
  CHECK(Minutes(1).value == 6000);

  CHECK(TickSpanFromMillisecondsExact(190).value == 19);
  CHECK(ToMilliseconds(TickSpan{19}) == 190);
  CHECK(ToMilliseconds(Tick{3}) == 30);
  CHECK(ToSeconds(Seconds(1)) == Approx(1.0f));
}

TEST_CASE("Tick rejects off-grid milliseconds", "[unit][foundation][tick]") {
  REQUIRE_THROWS_AS(TickSpanFromMillisecondsExact(195), std::invalid_argument);
  CHECK(TickSpanFromMillisecondsExact(0).value == 0);
}

TEST_CASE("Tick arithmetic", "[unit][foundation][tick]") {
  CHECK(Tick{100} - Tick{40} == TickSpan{60});
  CHECK(Tick{40} + TickSpan{60} == Tick{100});
  CHECK(TickSpan{30} + TickSpan{40} == TickSpan{70});
  CHECK(TickSpan{70} - TickSpan{40} == TickSpan{30});
}

TEST_CASE("Tick arithmetic rejects negative intervals", "[unit][foundation][tick]") {
  REQUIRE_THROWS_AS(Tick{40} - TickSpan{60}, std::invalid_argument);
  REQUIRE_THROWS_AS(TickSpan{10} - TickSpan{20}, std::invalid_argument);
}

TEST_CASE("Tick arithmetic rejects uint64 overflow", "[unit][foundation][tick]") {
  constexpr auto kMax = std::numeric_limits<std::uint64_t>::max();
  REQUIRE_THROWS_AS(Tick{kMax} + TickSpan{1}, std::overflow_error);
  REQUIRE_THROWS_AS(TickSpan{kMax} + TickSpan{1}, std::overflow_error);
  REQUIRE_THROWS_AS(Seconds(kMax), std::overflow_error);
  REQUIRE_THROWS_AS(ToMilliseconds(TickSpan{kMax}), std::overflow_error);
}
