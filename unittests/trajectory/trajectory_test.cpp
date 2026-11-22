#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "football/ball/ball.hpp"
#include "football/history/history.hpp"
#include "football/pitch/pitch.hpp"

using football::ball::Ball;
using football::ball::BallModel;
using football::history::History;
using football::pitch::Pitch;
using football::time::Tick;
using Catch::Approx;

namespace {

constexpr std::uint64_t kTicks = 1000;
constexpr BallModel kBall{.mass = 0.43f, .radius = 0.11f,
                          .magnus_coefficient = 0.02f};

std::filesystem::path TrajectoryPath(const std::string& name) {
  const auto directory = std::filesystem::current_path() / "trajectories";
  std::filesystem::create_directories(directory);
  return directory / (name + ".jsonl");
}

std::vector<nlohmann::json> LoadTrajectory(const std::filesystem::path& path) {
  std::ifstream input(path);
  REQUIRE(input);
  std::vector<nlohmann::json> lines;
  for (std::string line; std::getline(input, line);) {
    lines.push_back(nlohmann::json::parse(line));
  }
  return lines;
}

void RecordTrajectory(Ball& ball, const Pitch& pitch,
                     const std::filesystem::path& path, History& history) {
  history.RecordSnapshot(Tick{0}, ball.get_state());
  for (std::uint64_t tick = 1; tick <= kTicks; ++tick) {
    ball.Step(pitch);
    history.RecordSnapshot(Tick{tick}, ball.get_state());
  }
  history.SaveJsonl(path);
}

Ball GroundedBall() {
  Ball ball(kBall);
  ball.PlaceOnGround(0.0f, 0.0f);
  return ball;
}

}

TEST_CASE("a forward kick records a decelerating roll that stays on the grass") {
  const auto path = TrajectoryPath("forward");
  Ball ball = GroundedBall();
  ball.ApplyImpulseAtPoint({1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f});

  History history;
  RecordTrajectory(ball, Pitch{}, path, history);

  REQUIRE(history.ReadSnapshot(Tick{0}));
  CHECK(history.ReadSnapshot(Tick{0})->ball.position.coords[2] == Approx(kBall.radius));

  float previous_x = history.ReadSnapshot(Tick{0})->ball.position.coords[0];
  for (std::uint64_t tick = 1; tick <= kTicks; ++tick) {
    const auto frame = history.ReadSnapshot(Tick{tick});
    REQUIRE(frame);
    CHECK(frame->ball.position.coords[2] == Approx(kBall.radius).margin(1e-5f));
    CHECK(frame->ball.position.coords[0] >= previous_x);
    previous_x = frame->ball.position.coords[0];
  }

  const auto last = history.ReadSnapshot(Tick{kTicks});
  REQUIRE(last);
  CHECK(last->ball.position.coords[0] > 1.0f);
  CHECK(last->ball.position.coords[1] == Approx(0.0f).margin(1e-5f));
  CHECK(last->ball.velocity.coords[0] == Approx(0.0f).margin(1e-4f));

  const auto lines = LoadTrajectory(path);
  REQUIRE(lines.size() == kTicks + 2);
  CHECK(lines.front().at("type") == "header");
  CHECK(lines.at(1).at("tick") == 0);
  CHECK(lines.back().at("tick") == kTicks);
}

TEST_CASE("a lofted kick records a flight arc and settles back onto the grass") {
  const auto path = TrajectoryPath("lofted");
  Ball ball = GroundedBall();
  ball.ApplyImpulseAtPoint({0.5f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f});

  History history;
  RecordTrajectory(ball, Pitch{}, path, history);

  float highest = kBall.radius;
  std::uint64_t airborne_ticks = 0;
  for (std::uint64_t tick = 0; tick <= kTicks; ++tick) {
    const auto frame = history.ReadSnapshot(Tick{tick});
    REQUIRE(frame);
    const float height = frame->ball.position.coords[2];
    highest = std::max(highest, height);
    if (height > kBall.radius + 1e-4f) ++airborne_ticks;
  }
  CHECK(highest > 0.3f);
  CHECK(airborne_ticks > 10);

  const auto last = history.ReadSnapshot(Tick{kTicks});
  REQUIRE(last);
  CHECK(last->ball.position.coords[2] >= kBall.radius - 1e-5f);
  CHECK(last->ball.position.coords[2] <= kBall.radius + 5e-3f);

  const auto lines = LoadTrajectory(path);
  REQUIRE(lines.size() == kTicks + 2);
  CHECK(lines.at(1).at("position").at(2) == Approx(kBall.radius));
}

TEST_CASE("a diagonal kick records displacement on both horizontal axes") {
  const auto path = TrajectoryPath("diagonal");
  Ball ball = GroundedBall();
  ball.ApplyImpulseAtPoint({1.0f, 0.5f, 0.0f}, {0.0f, 0.0f, 0.0f});

  History history;
  RecordTrajectory(ball, Pitch{}, path, history);

  const auto first = history.ReadSnapshot(Tick{0});
  const auto last = history.ReadSnapshot(Tick{kTicks});
  REQUIRE(first);
  REQUIRE(last);
  CHECK(last->ball.position.coords[0] > first->ball.position.coords[0] + 0.5f);
  CHECK(last->ball.position.coords[1] > first->ball.position.coords[1] + 0.2f);
  CHECK(last->ball.position.coords[2] == Approx(kBall.radius).margin(1e-5f));
  CHECK(last->ball.velocity.GetLength() == Approx(0.0f).margin(1e-4f));

  for (std::uint64_t tick = 0; tick <= kTicks; ++tick) {
    const auto frame = history.ReadSnapshot(Tick{tick});
    REQUIRE(frame);
    CHECK(frame->ball.position.coords[2] == Approx(kBall.radius).margin(1e-5f));
    CHECK(frame->ball.position.coords[1] <= last->ball.position.coords[1] + 1e-5f);
  }

  const auto lines = LoadTrajectory(path);
  REQUIRE(lines.size() == kTicks + 2);
}

TEST_CASE("a saved trajectory round-trips every recorded tick exactly") {
  const auto path = TrajectoryPath("round_trip");
  Ball ball = GroundedBall();
  ball.ApplyImpulseAtPoint({0.8f, -0.3f, 0.6f}, {0.0f, kBall.radius, kBall.radius});

  History history;
  RecordTrajectory(ball, Pitch{}, path, history);
  const auto lines = LoadTrajectory(path);
  REQUIRE(lines.size() == kTicks + 2);

  const auto header = lines.front();
  CHECK(header.at("type") == "header");
  CHECK(header.at("version") == 1);
  CHECK(header.at("ticks_per_second") == football::time::kTicksPerSecond);

  for (std::uint64_t tick = 0; tick <= kTicks; ++tick) {
    const auto frame = history.ReadSnapshot(Tick{tick});
    REQUIRE(frame);
    const auto saved = lines.at(tick + 1);
    REQUIRE(saved.at("type") == "snapshot");
    REQUIRE(saved.at("tick") == tick);
    const auto& position = saved.at("position");
    const auto& velocity = saved.at("velocity");
    const auto& angular_velocity = saved.at("angular_velocity");
    const auto& orientation = saved.at("orientation");
    for (int axis = 0; axis < 3; ++axis) {
      CHECK(position.at(axis).get<float>() == frame->ball.position.coords[axis]);
      CHECK(velocity.at(axis).get<float>() == frame->ball.velocity.coords[axis]);
      CHECK(angular_velocity.at(axis).get<float>() ==
            frame->ball.angular_velocity.coords[axis]);
      CHECK(orientation.at(axis).get<float>() ==
            frame->ball.orientation.elements[axis]);
    }
    CHECK(orientation.at(3).get<float>() == frame->ball.orientation.elements[3]);
  }

}
