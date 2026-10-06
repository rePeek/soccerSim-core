#include <bit>
#include <limits>
#include <type_traits>

#include <catch2/catch_test_macros.hpp>

#include "sim/tick.hpp"
#include "sim/tick_boundary.hpp"

namespace {
using namespace football::sim;

template <typename Left, typename Right>
concept Addable = requires(Left left, Right right) { left + right; };
template <typename Left, typename Right>
concept AddAssignable = requires(Left left, Right right) { left += right; };

static_assert(Addable<Tick, TickSpan>);
static_assert(!Addable<Tick, Tick>);
static_assert(AddAssignable<Tick, TickSpan>);
static_assert(!AddAssignable<Tick, Tick>);
static_assert(!std::is_convertible_v<Tick, TickSpan>);
static_assert(!std::is_convertible_v<TickSpan, Tick>);
static_assert(!std::is_convertible_v<std::uint64_t, Tick>);
static_assert(!std::is_convertible_v<std::uint64_t, TickSpan>);
static_assert(std::is_same_v<decltype(Tick{} - Tick{}), TickSpan>);
static_assert(std::is_same_v<decltype(Tick{} + TickSpan{}), Tick>);
static_assert(Minutes(45) == TickSpan{270000});
static_assert(Minutes(5) == TickSpan{30000});
static_assert(Seconds(2) == TickSpan{200});
static_assert(Seconds(10) == TickSpan{1000});
static_assert(std::bit_cast<std::uint32_t>(kTickSeconds) ==
              std::bit_cast<std::uint32_t>(0.01f));

TEST_CASE("Tick arithmetic distinguishes instants from durations", "[tick]") {
  Tick now{23};
  const Tick deadline = now + Seconds(2);
  REQUIRE(deadline == Tick{223});
  REQUIRE(deadline - now == Seconds(2));
  REQUIRE(deadline - Seconds(2) == now);
  now += TickSpan{1};
  REQUIRE(now == Tick{24});

  TickSpan elapsed{1};
  elapsed += Seconds(10);
  REQUIRE(elapsed == TickSpan{1001});
  REQUIRE(elapsed - TickSpan{1} == Seconds(10));
  REQUIRE(Tick{} < now);
  REQUIRE(TickSpan{} < elapsed);
  REQUIRE(ToSeconds(TickSpan{1}) == kTickSeconds);
  REQUIRE(ToSeconds(Seconds(1)) == 1.0f);
}

TEST_CASE("Tick arithmetic rejects overflow and backwards intervals", "[tick]") {
  constexpr auto max = std::numeric_limits<std::uint64_t>::max();
  REQUIRE_THROWS_AS((Tick{max} + TickSpan{1}), std::overflow_error);
  REQUIRE_THROWS_AS((TickSpan{max} + TickSpan{1}), std::overflow_error);
  REQUIRE_THROWS_AS((Tick{1} - Tick{2}), std::invalid_argument);
  REQUIRE_THROWS_AS((Tick{1} - TickSpan{2}), std::invalid_argument);
  REQUIRE_THROWS_AS((TickSpan{1} - TickSpan{2}), std::invalid_argument);
  REQUIRE_THROWS_AS(Seconds(max), std::overflow_error);
  REQUIRE_THROWS_AS(Minutes(max), std::overflow_error);

  Tick now{max};
  REQUIRE_THROWS_AS(now += TickSpan{1}, std::overflow_error);
  REQUIRE(now.value == max);
  TickSpan duration{max};
  REQUIRE_THROWS_AS(duration += TickSpan{1}, std::overflow_error);
  REQUIRE(duration.value == max);
}

TEST_CASE("Millisecond boundaries convert exactly without silent rounding", "[tick]") {
  REQUIRE(TickSpanFromMillisecondsExact(0) == TickSpan{});
  REQUIRE(TickSpanFromMillisecondsExact(10) == TickSpan{1});
  REQUIRE(TickSpanFromMillisecondsExact(1900) == TickSpan{190});
  REQUIRE(TickSpanFromMillisecondsExact(2700000) == Minutes(45));
  REQUIRE(ToMilliseconds(TickSpan{1}) == 10);
  REQUIRE(ToMilliseconds(Minutes(45)) == 2700000);
  REQUIRE(ToMilliseconds(Tick{223}) == 2230);
  for (const auto value : {1, 9, 11, 703, 1551}) {
    REQUIRE_THROWS_AS(TickSpanFromMillisecondsExact(value), std::invalid_argument);
  }
  constexpr auto max = std::numeric_limits<std::uint64_t>::max();
  REQUIRE_THROWS_AS(ToMilliseconds(TickSpan{max}), std::overflow_error);
  REQUIRE_THROWS_AS(ToMilliseconds(Tick{max}), std::overflow_error);
}
}  // namespace
