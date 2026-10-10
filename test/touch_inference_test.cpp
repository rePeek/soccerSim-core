#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/default_ai.hpp"
#include "app/fixtures/default_teams.hpp"
#include "sim/observation/touch_inference.hpp"
#include "sim/simulation.hpp"
#include "sim/testing/simulation_access.hpp"

namespace {

using blunted::Vector3;
using football::model::PlayerId;
using football::sim::Tick;
using football::sim::observation::InferTouches;
using football::sim::observation::Snapshot;
using football::sim::observation::SnapshotMetadata;
using football::sim::observation::SnapshotRecord;
using football::sim::observation::TouchCandidate;
using football::sim::testing::SimulationAccess;

SnapshotRecord Record(std::uint64_t step, std::uint64_t generation, const Snapshot& snapshot) {
  SnapshotRecord record;
  record.stamp.step_index = step;
  record.stamp.timeline_tick = Tick{step};
  record.stamp.generation = generation;
  record.snapshot = snapshot;
  return record;
}

Snapshot EmptySnapshot(std::size_t players) {
  Snapshot snapshot;
  snapshot.players.resize(players);
  return snapshot;
}

struct TouchClip {
  std::uint32_t id = 0;
  int frame = 0;
};

// The baked asset is a regression input; the test only needs *a* clip that
// schedules a touch, not a specific one.
std::optional<TouchClip> FindTouchClip(const AnimationLibrary& animations) {
  for (std::uint32_t i = 0; i < animations.Size(); ++i) {
    const AnimationClip& clip = animations.Get(i);
    if (clip.metadata.touch_frame >= 1) return TouchClip{i, clip.metadata.touch_frame};
  }
  return std::nullopt;
}

PlayerControlSet NoControls() { return {}; }

}  // namespace

TEST_CASE("snapshot touch inference identifies a single animation touch",
          "[sim][observation][touch]") {
  Simulation simulation;
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(), football::model::Pitch{}, {});
  const auto& animations = SimulationAccess::AnimationLibraryOf(simulation);
  const SnapshotMetadata& metadata = simulation.SnapshotMetadata();
  const auto clip = FindTouchClip(animations);
  REQUIRE(clip.has_value());

  const std::size_t count = metadata.player_ids.size();
  Snapshot previous = EmptySnapshot(count);
  Snapshot current = EmptySnapshot(count);
  previous.ball.position = Vector3(0.5f, 0, 0);
  previous.ball.velocity = Vector3(0, 0, 0);
  current.ball.position = Vector3(1.0f, 0, 0);
  current.ball.velocity = Vector3(4, 0, 0);

  previous.players[0].active = true;
  previous.players[0].animation_id = static_cast<AnimationId>(clip->id);
  previous.players[0].frame = static_cast<std::uint32_t>(clip->frame - 1);
  previous.players[0].position = Vector3(0, 0, 0);
  current.players[0] = previous.players[0];
  current.players[0].frame = static_cast<std::uint32_t>(clip->frame);

  const SnapshotRecord before = Record(1, 7, previous);
  const SnapshotRecord after = Record(2, 7, current);
  const auto candidates = InferTouches(before, after, metadata, animations);
  REQUIRE(candidates.size() == 1);
  REQUIRE(candidates[0].player == metadata.player_ids[0]);
  REQUIRE(candidates[0].confidence > 0.0f);
  REQUIRE(candidates[0].confidence <= 1.0f);
}

TEST_CASE("snapshot touch inference is deterministic and spatial",
          "[sim][observation][touch]") {
  Simulation simulation;
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(), football::model::Pitch{}, {});
  const auto& animations = SimulationAccess::AnimationLibraryOf(simulation);
  const SnapshotMetadata& metadata = simulation.SnapshotMetadata();
  const auto clip = FindTouchClip(animations);
  REQUIRE(clip.has_value());

  const std::size_t count = metadata.player_ids.size();
  Snapshot previous = EmptySnapshot(count);
  Snapshot current = EmptySnapshot(count);
  previous.ball.position = Vector3(0.5f, 0, 0);
  current.ball.position = Vector3(1.0f, 0, 0);
  current.ball.velocity = Vector3(4, 0, 0);
  previous.players[0].active = true;
  previous.players[0].animation_id = static_cast<AnimationId>(clip->id);
  previous.players[0].frame = static_cast<std::uint32_t>(clip->frame - 1);
  previous.players[0].position = Vector3(0, 0, 0);
  current.players[0] = previous.players[0];
  current.players[0].frame = static_cast<std::uint32_t>(clip->frame);

  const SnapshotRecord before = Record(1, 3, previous);
  const SnapshotRecord after = Record(2, 3, current);
  const auto first = InferTouches(before, after, metadata, animations);
  const auto second = InferTouches(before, after, metadata, animations);
  REQUIRE(first.size() == second.size());
  for (std::size_t i = 0; i < first.size(); ++i) {
    REQUIRE(first[i].player == second[i].player);
    REQUIRE(first[i].confidence == second[i].confidence);
  }

  // A scheduled touch frame far from the ball is a fake, not a touch.
  Snapshot far = current;
  far.ball.position = Vector3(50, 0, 0);
  far.ball.velocity = Vector3(0, 0, 0);
  REQUIRE(InferTouches(before, Record(2, 3, far), metadata, animations).empty());
}

TEST_CASE("snapshot touch inference refuses unsafe snapshot pairs",
          "[sim][observation][touch]") {
  Simulation simulation;
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(), football::model::Pitch{}, {});
  const auto& animations = SimulationAccess::AnimationLibraryOf(simulation);
  const SnapshotMetadata& metadata = simulation.SnapshotMetadata();
  const auto clip = FindTouchClip(animations);
  REQUIRE(clip.has_value());

  const std::size_t count = metadata.player_ids.size();
  Snapshot previous = EmptySnapshot(count);
  Snapshot current = EmptySnapshot(count);
  previous.ball.position = Vector3(0.5f, 0, 0);
  current.ball.position = Vector3(1.0f, 0, 0);
  previous.players[0].active = true;
  previous.players[0].animation_id = static_cast<AnimationId>(clip->id);
  previous.players[0].frame = static_cast<std::uint32_t>(clip->frame - 1);
  current.players[0] = previous.players[0];
  current.players[0].frame = static_cast<std::uint32_t>(clip->frame);

  // Generation change (reset) is never inferred across.
  REQUIRE(InferTouches(Record(1, 1, previous), Record(2, 2, current), metadata, animations).empty());
  // Non-consecutive sampling is never bridged.
  REQUIRE(InferTouches(Record(1, 1, previous), Record(3, 1, current), metadata, animations).empty());
  // Topology mismatch is rejected.
  REQUIRE(InferTouches(Record(1, 1, previous), Record(2, 1, EmptySnapshot(count + 1)), metadata,
                       animations)
              .empty());
  // The animation changed within the step: the touch is not recoverable.
  Snapshot switched = current;
  switched.players[0].animation_id = -1;
  REQUIRE(InferTouches(Record(1, 1, previous), Record(2, 1, switched), metadata, animations).empty());
}

TEST_CASE("snapshot touch inference reports several candidates in one step",
          "[sim][observation][touch]") {
  Simulation simulation;
  simulation.Init(football::app::fixtures::MakeDefaultHomeTeam(),
      football::app::fixtures::MakeDefaultAwayTeam(), football::model::Pitch{}, {});
  const auto& animations = SimulationAccess::AnimationLibraryOf(simulation);
  const SnapshotMetadata& metadata = simulation.SnapshotMetadata();
  const auto clip = FindTouchClip(animations);
  REQUIRE(clip.has_value());

  const std::size_t count = metadata.player_ids.size();
  Snapshot previous = EmptySnapshot(count);
  Snapshot current = EmptySnapshot(count);
  previous.ball.position = Vector3(0.5f, 0, 0);
  current.ball.position = Vector3(1.0f, 0, 0);
  for (std::size_t slot : {std::size_t{0}, std::size_t{1}}) {
    previous.players[slot].active = true;
    previous.players[slot].animation_id = static_cast<AnimationId>(clip->id);
    previous.players[slot].frame = static_cast<std::uint32_t>(clip->frame - 1);
    previous.players[slot].position = Vector3(static_cast<float>(slot), 0, 0);
    current.players[slot] = previous.players[slot];
    current.players[slot].frame = static_cast<std::uint32_t>(clip->frame);
  }
  const auto candidates =
      InferTouches(Record(1, 1, previous), Record(2, 1, current), metadata, animations);
  REQUIRE(candidates.size() == 2);
  REQUIRE(candidates[0].player == metadata.player_ids[0]);
  REQUIRE(candidates[1].player == metadata.player_ids[1]);
}

TEST_CASE("snapshot touch inference shadow-compares with accepted touches",
          "[sim][observation][touch]") {
  const auto home = football::app::fixtures::MakeDefaultHomeTeam();
  const auto away = football::app::fixtures::MakeDefaultAwayTeam();
  Simulation simulation;
  simulation.Init(home, away, football::model::Pitch{}, {});
  football::ai::DefaultAI ai(home, away);
  const auto& animations = SimulationAccess::AnimationLibraryOf(simulation);
  const SnapshotMetadata& metadata = simulation.SnapshotMetadata();

  std::size_t compared = 0, matched = 0, missed = 0, spurious = 0;
  for (int step = 0; step < 4000; ++step) {
    const auto* latest = simulation.Snapshots().Latest();
    REQUIRE(latest != nullptr);
    const SnapshotRecord previous = *latest;

    PlayerControlSet controls;
    ai.Update(simulation.Observe(), controls);
    simulation.Step(controls);

    const auto* after = simulation.Snapshots().Latest();
    REQUIRE(after != nullptr);

    // Accepted touches of the step that produced `after`: the solver stamps a
    // touch with the instant the step began, which is the previous snapshot.
    std::vector<PlayerId> actual;
    for (int team = 0; team < 2; ++team) {
      for (Player* player : SimulationAccess::TeamOf(simulation, team)->GetAllPlayers()) {
        if (player->IsActive() && player->GetLastTouchTick() == previous.stamp.timeline_tick) {
          actual.push_back(player->GetID());
        }
      }
    }

    const std::vector<TouchCandidate> inferred =
        InferTouches(previous, *after, metadata, animations);
    ++compared;
    for (const TouchCandidate& candidate : inferred) {
      if (std::find(actual.begin(), actual.end(), candidate.player) != actual.end()) {
        ++matched;
      } else {
        ++spurious;
      }
    }
    for (PlayerId id : actual) {
      const bool found = std::any_of(inferred.begin(), inferred.end(),
                                     [id](const TouchCandidate& c) { return c.player == id; });
      if (!found) ++missed;
    }
  }

  WARN("touch inference shadow: compared=" << compared << " matched=" << matched
       << " missed=" << missed << " spurious=" << spurious);
  REQUIRE(compared == 4000);
}
