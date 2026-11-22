#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <thread>
#include <type_traits>
#include <utility>

#include "football/runner.hpp"

using football::ball::BallModel;
using football::history::HistoryMode;
using football::pitch::PitchModel;
using football::runner::SingleRunner;
using football::time::Tick;
using football::time::TickSpan;
using Catch::Approx;

static_assert(std::is_same_v<decltype(std::declval<const SingleRunner&>().Environment()),
                             const football::Env&>);

namespace {

constexpr PitchModel kPitch{.length = 105.0f, .width = 68.0f,
                            .quadratic_resistance = 0.04f,
                            .ground_deceleration = 1.6f, .air_drag = 0.015f};
constexpr BallModel kBall{.mass = 0.43f, .radius = 0.11f,
                          .magnus_coefficient = 0.02f};

}

TEST_CASE("a runner does not advance before Start") {
  SingleRunner runner(kPitch, kBall, TickSpan{100});
  runner.Wait();
  REQUIRE(runner.Environment().Ticks() == Tick{0});
  const auto opening = runner.Environment().ReadSnapshot(Tick{0});
  REQUIRE(opening);
  REQUIRE(opening->ball.position.coords[2] == kBall.radius);
}

TEST_CASE("a started runner executes its tick limit") {
  SingleRunner runner(kPitch, kBall, TickSpan{100});
  runner.Start();
  REQUIRE(runner.Environment().ReadSnapshot(Tick{0}));
  runner.Wait();
  REQUIRE(runner.Environment().Ticks() == Tick{100});
  const auto final_frame = runner.Environment().ReadSnapshot(Tick{100});
  REQUIRE(final_frame);
  REQUIRE(final_frame->tick == Tick{100});
  REQUIRE_FALSE(runner.Environment().ReadSnapshot(Tick{101}));
  REQUIRE(runner.Environment().frame_count() == 101);
}

TEST_CASE("snapshots stay readable while the worker records across reallocations") {
  SingleRunner runner(kPitch, kBall, TickSpan{400});
  std::atomic<bool> finished{false};
  std::atomic<bool> consistent{true};
  std::jthread reader([&] {
    while (!finished.load()) {
      const auto opening = runner.Environment().ReadSnapshot(Tick{0});
      if (!opening || opening->tick != Tick{0} ||
          opening->ball.position.coords[2] != kBall.radius) {
        consistent.store(false);
        return;
      }
      if (const auto final_frame = runner.Environment().ReadSnapshot(Tick{400})) {
        if (final_frame->tick != Tick{400}) consistent.store(false);
      }
    }
  });

  runner.Start();
  runner.Wait();
  finished.store(true);
  reader.join();

  REQUIRE(consistent.load());
  REQUIRE(runner.Environment().Ticks() == Tick{400});
  REQUIRE(runner.Environment().frame_count() == 401);
}

TEST_CASE("Start is one-shot and Wait is idempotent") {
  SingleRunner runner(kPitch, kBall, TickSpan{100});
  runner.Start();
  runner.Start();
  runner.Wait();
  runner.Start();
  runner.Wait();
  runner.Wait();
  REQUIRE(runner.Environment().Ticks() == Tick{100});
}

TEST_CASE("a zero tick limit keeps only the opening frame") {
  SingleRunner runner(kPitch, kBall);
  runner.Start();
  runner.Wait();
  REQUIRE(runner.Environment().Ticks() == Tick{0});
  REQUIRE(runner.Environment().ReadSnapshot(Tick{0}));
  REQUIRE_FALSE(runner.Environment().ReadSnapshot(Tick{1}));
}

TEST_CASE("runners own independent matches and tick limits") {
  SingleRunner first(kPitch, kBall, TickSpan{100});
  SingleRunner second(kPitch, kBall, TickSpan{200});
  first.Start();
  second.Start();
  first.Wait();
  second.Wait();
  REQUIRE(first.Environment().Ticks() == Tick{100});
  REQUIRE(second.Environment().Ticks() == Tick{200});
  REQUIRE(&first.Environment() != &second.Environment());
}

TEST_CASE("a runner given a replay history does not advance the match") {
  const auto path = std::filesystem::temp_directory_path() / "football_runner_replay.jsonl";
  football::history::History recording;
  football::ball::BallState state;
  state.position = football::math::Vector3(3.0f, 4.0f, 0.11f);
  recording.RecordSnapshot(Tick{0}, state);
  recording.SaveJsonl(path);

  SingleRunner runner(kPitch, kBall, TickSpan{100}, path);
  runner.Start();
  runner.Wait();

  const auto& env = runner.Environment();
  REQUIRE(env.Ticks() == Tick{0});
  REQUIRE(env.history_mode() == HistoryMode::Replay);
  REQUIRE(env.ReadSnapshot(Tick{0})->ball.position.coords[1] == Approx(4.0f));
  REQUIRE_FALSE(env.ReadSnapshot(Tick{1}));

  std::filesystem::remove(path);
}
