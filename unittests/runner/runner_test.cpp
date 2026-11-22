#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <type_traits>
#include <utility>

#include "football/runner.hpp"

using football::ball::BallModel;
using football::history::Recording;
using football::pitch::PitchModel;
using football::runner::SingleRunner;
using football::time::Tick;
using football::time::TickSpan;

static_assert(std::is_same_v<decltype(std::declval<SingleRunner&>().EnvironmentAfterJoin()),
                             const football::Env&>);
static_assert(std::is_same_v<decltype(std::declval<const SingleRunner&>().history()),
                             const football::history::History&>);

namespace {

constexpr PitchModel kPitch{.length = 105.0f, .width = 68.0f,
                            .quadratic_resistance = 0.04f,
                            .ground_deceleration = 1.6f, .air_drag = 0.015f};
constexpr BallModel kBall{.mass = 0.43f, .radius = 0.11f,
                          .magnus_coefficient = 0.02f};

}

TEST_CASE("a runner does not advance before Start") {
  SingleRunner runner(kPitch, kBall, Recording::Enabled, TickSpan{100});
  runner.Wait();
  REQUIRE(runner.EnvironmentAfterJoin().Ticks() == Tick{0});
  const auto opening = runner.history().ReadSnapshot(Tick{0});
  REQUIRE(opening);
  REQUIRE(opening->ball.position.coords[2] == kBall.radius);
}

TEST_CASE("a started runner executes its tick limit and rejects access until joined") {
  SingleRunner runner(kPitch, kBall, Recording::Enabled, TickSpan{100});
  const SingleRunner& reader = runner;
  runner.Start();
  REQUIRE_THROWS_AS(reader.EnvironmentAfterJoin(), std::logic_error);
  REQUIRE(reader.history().ReadSnapshot(Tick{0}));
  runner.Wait();
  REQUIRE(reader.EnvironmentAfterJoin().Ticks() == Tick{100});
  const auto final_frame = reader.history().ReadSnapshot(Tick{100});
  REQUIRE(final_frame);
  REQUIRE(final_frame->tick == Tick{100});
  REQUIRE_FALSE(reader.history().ReadSnapshot(Tick{101}));
  REQUIRE(&reader.history() == &reader.EnvironmentAfterJoin().history());
}

TEST_CASE("Start is one-shot and Wait is idempotent") {
  SingleRunner runner(kPitch, kBall, Recording::Disabled, TickSpan{100});
  runner.Start();
  runner.Start();
  runner.Wait();
  runner.Start();
  runner.Wait();
  runner.Wait();
  REQUIRE(runner.EnvironmentAfterJoin().Ticks() == Tick{100});
}

TEST_CASE("a zero tick limit keeps only the opening frame") {
  SingleRunner runner(kPitch, kBall);
  runner.Start();
  runner.Wait();
  REQUIRE(runner.EnvironmentAfterJoin().Ticks() == Tick{0});
  REQUIRE(runner.history().ReadSnapshot(Tick{0}));
  REQUIRE_FALSE(runner.history().ReadSnapshot(Tick{1}));
}

TEST_CASE("runners own independent matches and tick limits") {
  SingleRunner first(kPitch, kBall, Recording::Disabled, TickSpan{100});
  SingleRunner second(kPitch, kBall, Recording::Disabled, TickSpan{200});
  first.Start();
  second.Start();
  first.Wait();
  second.Wait();
  REQUIRE(first.EnvironmentAfterJoin().Ticks() == Tick{100});
  REQUIRE(second.EnvironmentAfterJoin().Ticks() == Tick{200});
  REQUIRE(&first.EnvironmentAfterJoin() != &second.EnvironmentAfterJoin());
}

TEST_CASE("a runner can advance without recording") {
  SingleRunner runner(kPitch, kBall, Recording::Disabled, TickSpan{100});
  runner.Start();
  runner.Wait();
  REQUIRE(runner.EnvironmentAfterJoin().Ticks() == Tick{100});
  REQUIRE_FALSE(runner.history().enabled());
  REQUIRE_FALSE(runner.history().ReadSnapshot(Tick{0}));
  REQUIRE_FALSE(runner.history().ReadSnapshot(Tick{100}));
}
