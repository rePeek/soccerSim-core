#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>

#include "football/env.hpp"

using football::ball::BallModel;
using football::Env;
using football::pitch::PitchModel;
using football::time::Tick;
using football::time::TickSpan;
using Catch::Approx;

namespace {

constexpr PitchModel kPitch{.length = 105.0f, .width = 68.0f,
                            .quadratic_resistance = 0.04f,
                            .ground_deceleration = 1.6f, .air_drag = 0.015f};
constexpr BallModel kBall{.mass = 0.43f, .radius = 0.11f,
                          .magnus_coefficient = 0.02f};

}

TEST_CASE("a new environment starts at tick zero with an opening frame") {
  Env env(kPitch, kBall);
  REQUIRE(env.Ticks() == Tick{0});
  const auto opening = env.ReadSnapshot(Tick{0});
  REQUIRE(opening);
  REQUIRE(opening->tick == Tick{0});
  REQUIRE(opening->ball.position.coords[0] == Approx(0.0f));
  REQUIRE(opening->ball.position.coords[1] == Approx(0.0f));
  REQUIRE(opening->ball.position.coords[2] == Approx(kBall.radius));
}

TEST_CASE("LoopMain records every committed tick and preserves earlier frames") {
  Env env(kPitch, kBall);
  env.LoopMain(TickSpan{1});
  const auto recorded = env.ReadSnapshot(Tick{1});
  REQUIRE(recorded);
  env.LoopMain(TickSpan{50});
  REQUIRE(env.Ticks() == Tick{51});
  REQUIRE(env.ReadSnapshot(Tick{1}) == recorded);
  for (std::uint64_t tick = 0; tick <= 51; ++tick) {
    const auto frame = env.ReadSnapshot(Tick{tick});
    REQUIRE(frame);
    REQUIRE(frame->tick == Tick{tick});
  }
  REQUIRE_FALSE(env.ReadSnapshot(Tick{52}));
}

TEST_CASE("LoopMain advances exactly the requested ticks including zero") {
  Env env(kPitch, kBall);
  env.LoopMain(TickSpan{});
  REQUIRE(env.Ticks() == Tick{0});
  REQUIRE_FALSE(env.ReadSnapshot(Tick{1}));
  env.LoopMain(TickSpan{500});
  REQUIRE(env.Ticks() == Tick{500});
  env.LoopMain(TickSpan{});
  REQUIRE(env.Ticks() == Tick{500});
  env.LoopMain(TickSpan{250});
  REQUIRE(env.Ticks() == Tick{750});
}

TEST_CASE("equal inputs record equal frames") {
  Env first(kPitch, kBall);
  Env second(kPitch, kBall);
  first.LoopMain(TickSpan{600});
  second.LoopMain(TickSpan{600});
  for (std::uint64_t tick = 0; tick <= 600; ++tick) {
    const auto frame = first.ReadSnapshot(Tick{tick});
    REQUIRE(frame);
    REQUIRE(second.ReadSnapshot(Tick{tick}) == frame);
  }
}

TEST_CASE("environments are independent") {
  Env first(kPitch, kBall);
  Env second(kPitch, kBall);
  first.LoopMain(TickSpan{7});
  REQUIRE(second.Ticks() == Tick{0});
  second.LoopMain(TickSpan{3});
  REQUIRE(first.Ticks() == Tick{7});
  REQUIRE(second.Ticks() == Tick{3});
}

TEST_CASE("LoopMain has no built-in match horizon") {
  Env env(kPitch, kBall);
  const auto duration = football::time::Minutes(120);
  env.LoopMain(duration);
  REQUIRE(env.Ticks() == Tick{duration.value});
  env.LoopMain(TickSpan{1000});
  REQUIRE(env.Ticks() == Tick{duration.value + 1000});
}

TEST_CASE("an environment given a replay history does not simulate") {
  const auto path = std::filesystem::temp_directory_path() / "football_env_replay.jsonl";
  football::history::History recording;
  football::ball::BallState state;
  state.position = football::math::Vector3(1.0f, 2.0f, 0.11f);
  recording.RecordSnapshot(Tick{0}, state);
  recording.SaveJsonl(path);

  Env env(kPitch, kBall, path);
  REQUIRE(env.history_mode() == football::history::HistoryMode::Replay);
  env.LoopMain(TickSpan{100});
  REQUIRE(env.Ticks() == Tick{0});
  REQUIRE(env.ReadSnapshot(Tick{0}));
  REQUIRE(env.ReadSnapshot(Tick{0})->ball.position.coords[0] == Approx(1.0f));
  REQUIRE_FALSE(env.ReadSnapshot(Tick{1}));

  std::filesystem::remove(path);
}
