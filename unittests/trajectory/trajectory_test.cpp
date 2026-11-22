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

constexpr std::uint64_t kTicks = 1500;
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
  const auto last = history.ReadSnapshot(Tick{kTicks});
  const auto previous = history.ReadSnapshot(Tick{kTicks - 1});
  REQUIRE(last);
  REQUIRE(previous);
  CHECK(last->ball.velocity.GetLength() == Approx(0.0f).margin(1e-4f));
  CHECK(last->ball.angular_velocity.GetLength() == Approx(0.0f).margin(1e-4f));
  CHECK(last->ball.position == previous->ball.position);
  CHECK(last->ball.orientation == previous->ball.orientation);
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
  ball.ApplyImpulseAtPoint({5.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f});

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
  CHECK(last->ball.position.coords[0] > 15.0f);
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
  const football::math::Vector3 impulse{3.0f, 0.0f, 2.5f};
  const auto contact = ball.get_state().position - impulse.GetNormalized() * kBall.radius;
  ball.ApplyImpulseAtPoint(impulse, contact);

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
  CHECK(highest > 1.0f);
  CHECK(airborne_ticks > 100);

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
  ball.ApplyImpulseAtPoint({5.0f, 2.5f, 0.0f}, {0.0f, 0.0f, 0.0f});

  History history;
  RecordTrajectory(ball, Pitch{}, path, history);

  const auto first = history.ReadSnapshot(Tick{0});
  const auto last = history.ReadSnapshot(Tick{kTicks});
  REQUIRE(first);
  REQUIRE(last);
  CHECK(last->ball.position.coords[0] > first->ball.position.coords[0] + 15.0f);
  CHECK(last->ball.position.coords[1] > first->ball.position.coords[1] + 7.0f);
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
  ball.ApplyImpulseAtPoint({4.0f, -1.5f, 3.0f}, {0.0f, kBall.radius, kBall.radius});

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

  const History loaded = History::LoadJsonl(path);
  REQUIRE(loaded.mode() == football::history::HistoryMode::Replay);
  for (std::uint64_t tick = 0; tick <= kTicks; ++tick) {
    REQUIRE(loaded.ReadSnapshot(Tick{tick}) == history.ReadSnapshot(Tick{tick}));
  }
  REQUIRE_FALSE(loaded.ReadSnapshot(Tick{kTicks + 1}));
}

TEST_CASE("a long pass flies across the pitch then bounces and rolls to rest") {
  const auto path = TrajectoryPath("long_pass");
  Ball ball(kBall);
  ball.PlaceOnGround(-25.0f, -12.0f);
  const auto start = ball.get_state().position;
  const football::math::Vector3 impulse{8.6f, 2.58f, 5.16f};
  const auto contact_direction =
      (impulse.GetNormalized() * -1.0f + football::math::Vector3(0, 0, -0.03f)).GetNormalized();
  const auto contact = start + contact_direction * kBall.radius;
  ball.ApplyImpulseAtPoint(impulse, contact);
  const auto initial = ball.get_state();
  CHECK(initial.angular_velocity.GetLength() > 5.0f);
  CHECK(initial.angular_velocity.GetLength() < 10.0f);
  CHECK(initial.angular_velocity.coords[1] < 0.0f);
  CHECK(initial.angular_velocity.GetCrossProduct(initial.velocity).coords[2] > 0.0f);

  const Pitch pitch;
  History history;
  RecordTrajectory(ball, pitch, path, history);

  float highest = kBall.radius;
  std::uint64_t first_landing = 0;
  bool airborne = false;
  int bounces = 0;
  for (std::uint64_t tick = 1; tick <= kTicks; ++tick) {
    const auto frame = history.ReadSnapshot(Tick{tick});
    const auto previous = history.ReadSnapshot(Tick{tick - 1});
    REQUIRE(frame);
    REQUIRE(previous);
    const auto& state = frame->ball;
    highest = std::max(highest, state.position.coords[2]);
    CHECK(state.position.coords[2] >= kBall.radius - 1e-5f);
    CHECK(pitch.contains(state.position.coords[0], state.position.coords[1]));
    if (first_landing == 0 && airborne && state.position.coords[2] > kBall.radius + 1e-4f) {
      CHECK(state.angular_velocity.GetLength() < previous->ball.angular_velocity.GetLength());
      CHECK(state.angular_velocity.GetDotProduct(initial.angular_velocity) > 0.0f);
      CHECK(state.orientation != previous->ball.orientation);
    }
    if (state.position.coords[2] > kBall.radius + 1e-4f) airborne = true;
    if (airborne && first_landing == 0 &&
        state.position.coords[2] <= kBall.radius + 1e-4f) {
      first_landing = tick;
    }
    if (previous->ball.velocity.coords[2] < 0.0f && state.velocity.coords[2] > 0.0f) {
      ++bounces;
    }
  }

  CHECK(highest > 4.0f);
  CHECK(highest < 10.0f);
  REQUIRE(first_landing > 150);
  REQUIRE(first_landing < 300);
  const auto landing = history.ReadSnapshot(Tick{first_landing});
  const auto last = history.ReadSnapshot(Tick{kTicks});
  REQUIRE(landing);
  REQUIRE(last);
  CHECK(landing->ball.position.coords[0] - start.coords[0] > 25.0f);
  CHECK(last->ball.position.coords[0] - start.coords[0] > 40.0f);
  CHECK(last->ball.position.coords[1] - start.coords[1] > 10.0f);
  CHECK(last->ball.position.coords[0] > landing->ball.position.coords[0] + 1.0f);
  CHECK(bounces >= 2);

  const History loaded = History::LoadJsonl(path);
  CHECK(loaded.ReadSnapshot(Tick{0}) == history.ReadSnapshot(Tick{0}));
  CHECK(loaded.ReadSnapshot(Tick{first_landing}) == landing);
  CHECK(loaded.ReadSnapshot(Tick{kTicks}) == last);
}
