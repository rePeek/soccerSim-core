#include <catch2/catch_test_macros.hpp>

#include <barrier>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "football/history/history.hpp"

using football::ball::BallState;
using football::history::History;
using football::history::Recording;
using football::history::Snapshot;
using football::math::Quaternion;
using football::math::Vector3;
using football::time::Tick;

namespace {

BallState BallAt(float x) {
  BallState state;
  state.position = Vector3(x, 0.0f, 0.11f);
  return state;
}

}

TEST_CASE("a recorder starts empty") {
  const History history;
  REQUIRE(history.enabled());
  REQUIRE_FALSE(history.ReadSnapshot(Tick{0}));
}

TEST_CASE("recording stores a copy of the state") {
  History history;
  BallState state = BallAt(1.0f);
  history.RecordSnapshot(Tick{0}, state);

  const auto frame = history.ReadSnapshot(Tick{0});
  REQUIRE(frame);
  REQUIRE(frame->tick == Tick{0});
  REQUIRE(frame->ball == state);

  state.position = Vector3(2.0f, 0.0f, 0.11f);
  REQUIRE(frame->ball.position.coords[0] == 1.0f);
}

TEST_CASE("recording requires consecutive ticks starting at zero") {
  History history;
  REQUIRE_THROWS_AS(history.RecordSnapshot(Tick{100}, BallAt(0.0f)),
                    std::invalid_argument);
  REQUIRE_FALSE(history.ReadSnapshot(Tick{0}));

  history.RecordSnapshot(Tick{0}, BallAt(0.0f));
  REQUIRE_THROWS_AS(history.RecordSnapshot(Tick{0}, BallAt(1.0f)),
                    std::invalid_argument);
  REQUIRE_THROWS_AS(history.RecordSnapshot(Tick{2}, BallAt(2.0f)),
                    std::invalid_argument);
  REQUIRE_FALSE(history.ReadSnapshot(Tick{1}));

  history.RecordSnapshot(Tick{1}, BallAt(1.0f));
  for (std::uint64_t tick = 0; tick < 2; ++tick) {
    const auto snapshot = history.ReadSnapshot(Tick{tick});
    REQUIRE(snapshot);
    REQUIRE(snapshot->tick == Tick{tick});
    REQUIRE(snapshot->ball == BallAt(static_cast<float>(tick)));
  }
}

TEST_CASE("explicitly enabled recording starts empty and accepts frames") {
  History history(Recording::Enabled);
  REQUIRE_FALSE(history.ReadSnapshot(Tick{0}));
  history.RecordSnapshot(Tick{0}, BallAt(1.0f));
  const auto frame = history.ReadSnapshot(Tick{0});
  REQUIRE(frame);
  REQUIRE(frame->ball == BallAt(1.0f));
}

TEST_CASE("a disabled recorder drops every frame") {
  History history(Recording::Disabled);
  history.RecordSnapshot(Tick{1}, BallAt(0.0f));
  REQUIRE_FALSE(history.enabled());
  REQUIRE_FALSE(history.ReadSnapshot(Tick{0}));
  REQUIRE_FALSE(history.ReadSnapshot(Tick{1}));
}

TEST_CASE("snapshot reads return independent copies or nullopt") {
  History history;
  REQUIRE_FALSE(history.ReadSnapshot(Tick{0}));

  auto state = BallAt(1.0f);
  state.orientation.Set(0.1f, 0.2f, 0.3f, 0.9f);
  history.RecordSnapshot(Tick{0}, state);
  history.RecordSnapshot(Tick{1}, BallAt(2.0f));
  const History& reader = history;
  auto snapshot = reader.ReadSnapshot(Tick{0});
  REQUIRE(snapshot);
  REQUIRE(snapshot->tick == Tick{0});
  REQUIRE(snapshot->ball == state);
  snapshot->ball.position = Vector3(42.0f, 0.0f, 0.0f);
  snapshot = reader.ReadSnapshot(Tick{0});
  REQUIRE(snapshot);
  REQUIRE(snapshot->ball == state);
  snapshot = reader.ReadSnapshot(Tick{1});
  REQUIRE(snapshot);
  REQUIRE(snapshot->tick == Tick{1});
  REQUIRE(snapshot->ball == BallAt(2.0f));
  REQUIRE_FALSE(reader.ReadSnapshot(Tick{2}));
  REQUIRE_FALSE(reader.ReadSnapshot(Tick{UINT64_MAX}));

  const History disabled(Recording::Disabled);
  REQUIRE_FALSE(disabled.ReadSnapshot(Tick{0}));
}

TEST_CASE("snapshot reads race with recording across chunk reallocations") {
  constexpr std::uint64_t frame_count = 2000;
  History history;
  std::barrier phase(2);
  std::jthread writer([&] {
    for (std::uint64_t tick = 0; tick < frame_count; ++tick) {
      phase.arrive_and_wait();
      history.RecordSnapshot(Tick{tick}, BallAt(static_cast<float>(tick)));
      phase.arrive_and_wait();
    }
  });

  bool snapshots_match_requested_ticks = true;
  for (std::uint64_t tick = 0; tick < frame_count; ++tick) {
    phase.arrive_and_wait();
    const auto concurrent_snapshot = history.ReadSnapshot(Tick{tick});
    phase.arrive_and_wait();
    const auto committed_snapshot = history.ReadSnapshot(Tick{tick});
    const auto matches = [tick](const auto& snapshot) {
      return snapshot && snapshot->tick == Tick{tick} &&
             snapshot->ball == BallAt(static_cast<float>(tick));
    };
    snapshots_match_requested_ticks &=
        (!concurrent_snapshot || matches(concurrent_snapshot)) &&
        matches(committed_snapshot);
  }
  writer.join();
  REQUIRE(snapshots_match_requested_ticks);
  REQUIRE_FALSE(history.ReadSnapshot(Tick{frame_count}));
}

TEST_CASE("a saved history is a header line followed by one line per tick") {
  const auto path = std::filesystem::temp_directory_path() / "football_history_save.jsonl";
  History history;
  auto state = BallAt(1.5f);
  state.velocity = Vector3(0.1f, 0.2f, 0.3f);
  state.angular_velocity = Vector3(0.4f, 0.5f, 0.6f);
  state.orientation = Quaternion(0.1f, 0.2f, 0.3f, 0.9f);
  history.RecordSnapshot(Tick{0}, state);
  history.RecordSnapshot(Tick{1}, BallAt(2.5f));

  history.SaveJsonl(path);

  std::ifstream input(path);
  REQUIRE(input);
  std::vector<std::string> lines;
  for (std::string line; std::getline(input, line);) lines.push_back(line);
  REQUIRE(lines.size() == 3);

  const auto header = nlohmann::json::parse(lines[0]);
  CHECK(header.at("type") == "header");
  CHECK(header.at("version") == 1);
  CHECK(header.at("ticks_per_second") == football::time::kTicksPerSecond);

  const auto first = nlohmann::json::parse(lines[1]);
  CHECK(first.at("type") == "snapshot");
  CHECK(first.at("tick") == 0);
  CHECK(first.at("position") == nlohmann::json::array({1.5f, 0.0f, 0.11f}));
  CHECK(first.at("velocity") == nlohmann::json::array({0.1f, 0.2f, 0.3f}));
  CHECK(first.at("angular_velocity") == nlohmann::json::array({0.4f, 0.5f, 0.6f}));
  CHECK(first.at("orientation") == nlohmann::json::array({0.1f, 0.2f, 0.3f, 0.9f}));

  const auto second = nlohmann::json::parse(lines[2]);
  CHECK(second.at("tick") == 1);
  CHECK(second.at("position") == nlohmann::json::array({2.5f, 0.0f, 0.11f}));

  std::filesystem::remove(path);
}

TEST_CASE("saved float values round-trip exactly") {
  const auto path = std::filesystem::temp_directory_path() / "football_history_round_trip.jsonl";
  History history;
  BallState state;
  state.position = Vector3(1.0f / 3.0f, -std::numeric_limits<float>::max(),
                          std::numeric_limits<float>::denorm_min());
  state.velocity = Vector3(-0.0f, std::numeric_limits<float>::min(), 1e-30f);
  state.angular_velocity = Vector3(123456.789f, -7.5f, 0.1f);
  state.orientation = Quaternion(0.25f, -0.5f, 0.75f, 0.5f);
  history.RecordSnapshot(Tick{0}, state);

  history.SaveJsonl(path);

  std::ifstream input(path);
  REQUIRE(input);
  std::string header_line;
  std::string snapshot_line;
  std::getline(input, header_line);
  std::getline(input, snapshot_line);
  const auto saved = nlohmann::json::parse(snapshot_line);
  const auto saved_position = saved.at("position");
  const auto saved_velocity = saved.at("velocity");
  const auto saved_angular = saved.at("angular_velocity");
  const auto saved_orientation = saved.at("orientation");
  for (int index = 0; index < 3; ++index) {
    CHECK(saved_position.at(index).get<float>() == state.position.coords[index]);
    CHECK(saved_velocity.at(index).get<float>() == state.velocity.coords[index]);
    CHECK(saved_angular.at(index).get<float>() == state.angular_velocity.coords[index]);
    CHECK(saved_orientation.at(index).get<float>() == state.orientation.elements[index]);
  }
  CHECK(saved_orientation.at(3).get<float>() == state.orientation.elements[3]);

  std::filesystem::remove(path);
}

TEST_CASE("saving a recorder without frames writes only the header") {
  const auto path = std::filesystem::temp_directory_path() / "football_history_empty.jsonl";
  const History history(Recording::Disabled);

  history.SaveJsonl(path);

  std::ifstream input(path);
  REQUIRE(input);
  std::string header_line;
  std::string extra_line;
  REQUIRE(std::getline(input, header_line));
  CHECK_FALSE(std::getline(input, extra_line));
  CHECK(nlohmann::json::parse(header_line).at("type") == "header");

  std::filesystem::remove(path);
}

TEST_CASE("saving to an unavailable path throws and leaves no file behind") {
  const auto directory = std::filesystem::temp_directory_path() / "football_history_missing";
  const auto path = directory / "history.jsonl";
  std::filesystem::remove_all(directory);
  History history;
  history.RecordSnapshot(Tick{0}, BallAt(1.0f));

  CHECK_THROWS_AS(history.SaveJsonl(path), std::runtime_error);
  CHECK_FALSE(std::filesystem::exists(path));
}
