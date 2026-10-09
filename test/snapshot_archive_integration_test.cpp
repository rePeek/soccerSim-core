#include <limits>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>

#include <catch2/catch_test_macros.hpp>

#include "app/fixtures/default_teams.hpp"
#include "app/recording/snapshot_archive.hpp"
#include "env.hpp"

TEST_CASE("GameEnv archives exactly the committed samples beyond memory retention", "[snapshot][archive][integration]") {
  using namespace football::sim::observation;
  using namespace football::app::recording;
  MatchOptions options;
  options.half_duration = football::sim::TickSpan{100};
  options.snapshot_capacity = 1;
  GameEnv game{football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(), {}, {}, options, {}};
  SnapshotRecord sample;
  REQUIRE_THROWS_AS(game.CopyLatestSnapshot(sample), std::logic_error);
  REQUIRE_THROWS_AS(game.SnapshotMetadata(), std::logic_error);
  std::vector<SnapshotRecord> window;
  REQUIRE_THROWS_AS(game.CopySnapshotWindow(1, window), std::logic_error);
  REQUIRE_THROWS_AS(game.EventTrajectories(), std::logic_error);
  const auto path = std::filesystem::temp_directory_path() /
      ("soccer-snapshot-integration-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()) + ".snap");
  struct Remove {
    std::filesystem::path path;
    ~Remove() { std::error_code ignored; std::filesystem::remove(path, ignored); }
  } cleanup{path};
  std::vector<char> first_bytes;
  std::optional<MatchResult> first_result;
  for (int run = 0; run < 2; ++run) {
    game.Start();
    const auto metadata = game.SnapshotMetadata();
    SnapshotArchive archive;
    archive.Open(path, metadata, {100, 2});
    std::vector<SnapshotRecord> expected;
    game.CopyLatestSnapshot(sample);
    REQUIRE(sample.stamp.step_index == 0);
    REQUIRE(game.EventTrajectories().empty());
    REQUIRE(game.CopySnapshotWindow(1, window));
    REQUIRE(window.front().stamp.step_index == 0);
    archive.Append(sample);
    expected.push_back(sample);
    while (!game.Finished()) {
      game.Step();
      game.CopyLatestSnapshot(sample);
      REQUIRE(sample.stamp.step_index == expected.size());
      REQUIRE_FALSE(game.CopySnapshotWindow(2, window));
      REQUIRE(game.CopySnapshotWindow(1, window));
      REQUIRE(window.front().stamp.step_index == sample.stamp.step_index);
      archive.Append(sample);
      expected.push_back(sample);
      REQUIRE(expected.size() < 3000);
    }
    archive.Close();
    const auto result = game.Result();
    REQUIRE(expected.size() == result.duration_ticks + 1);
    game.Step();
    game.CopyLatestSnapshot(sample);
    REQUIRE(sample.stamp.step_index == result.duration_ticks);
    game.Stop();
    REQUIRE_THROWS_AS(game.CopyLatestSnapshot(sample), std::logic_error);
    REQUIRE_THROWS_AS(game.CopySnapshotWindow(1, window), std::logic_error);
    REQUIRE_THROWS_AS(game.EventTrajectories(), std::logic_error);
    REQUIRE(metadata.player_ids.size() == 22); // Owning metadata survives Stop.
    SnapshotArchiveReader reader;
    reader.Open(path, metadata.animation_library_hash);
    REQUIRE(reader.Complete());
    REQUIRE(reader.RecordCount() == expected.size());
    const auto replay = reader.ReadRange(0, result.duration_ticks);
    REQUIRE(replay.size() == expected.size());
    for (std::size_t i = 0; i < replay.size(); ++i) {
      REQUIRE(replay[i].stamp.step_index == expected[i].stamp.step_index);
      REQUIRE(replay[i].stamp.timeline_tick == expected[i].stamp.timeline_tick);
      REQUIRE(replay[i].stamp.generation == expected[i].stamp.generation);
      REQUIRE(replay[i].snapshot.ball.position == expected[i].snapshot.ball.position);
      REQUIRE(replay[i].snapshot.ball.velocity == expected[i].snapshot.ball.velocity);
      REQUIRE(replay[i].snapshot.ball.angular_velocity == expected[i].snapshot.ball.angular_velocity);
      for (std::size_t p = 0; p < metadata.player_ids.size(); ++p) {
        REQUIRE(replay[i].snapshot.players[p].position == expected[i].snapshot.players[p].position);
        REQUIRE(replay[i].snapshot.players[p].animation_id == expected[i].snapshot.players[p].animation_id);
        REQUIRE(replay[i].snapshot.players[p].frame == expected[i].snapshot.players[p].frame);
      }
    }
    std::ifstream stream(path, std::ios::binary);
    std::vector<char> bytes(std::istreambuf_iterator<char>(stream), {});
    if (run == 0) { first_bytes = bytes; first_result = result; }
    else { REQUIRE(bytes == first_bytes); REQUIRE(result == *first_result); }
  }
}

#ifdef FOOTBALL_TEST_CLI_SNAPSHOT_PATH
TEST_CASE("real CLI archive has the initial frame every executed step and terminal frame", "[.archive-cli]") {
  football::app::recording::SnapshotArchiveReader reader;
  reader.Open(FOOTBALL_TEST_CLI_SNAPSHOT_PATH);
  REQUIRE(reader.Complete());
  REQUIRE(reader.Metadata().animation_library_hash != 0);
  REQUIRE(reader.Metadata().player_ids.size() == 22);
  const auto frames = reader.ReadRange(0, std::numeric_limits<std::uint64_t>::max());
  REQUIRE(frames.size() > 3);
  REQUIRE(reader.RecordCount() == frames.size());
  REQUIRE(frames.front().stamp.step_index == 0);
  for (std::size_t i = 0; i < frames.size(); ++i) REQUIRE(frames[i].stamp.step_index == i);
  REQUIRE(frames.back().stamp.timeline_tick == frames[frames.size() - 2].stamp.timeline_tick);
}
#endif
