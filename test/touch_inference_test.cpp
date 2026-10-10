#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <sstream>
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

namespace {

// Mirrors the solver's crossing test for diagnosis; keep in sync with
// touch_inference.cpp.
bool TestCrossedTouchFrame(const football::sim::observation::PlayerSnapshot& previous,
                           const football::sim::observation::PlayerSnapshot& current,
                           const AnimationClip& clip) {
  if (previous.animation_id != current.animation_id) return false;
  const auto crossed = [&](int frame) {
    return frame >= 0 && previous.frame < static_cast<std::uint32_t>(frame) &&
           static_cast<std::uint32_t>(frame) <= current.frame;
  };
  if (crossed(clip.metadata.touch_frame)) return true;
  for (const BakedTouch& touch : clip.touches) {
    if (crossed(touch.frame)) return true;
  }
  return false;
}

const char* SourceName(e_TouchType type) {
  switch (type) {
    case e_TouchType_Intentional_Kicked: return "intentional kicked";
    case e_TouchType_Intentional_Nonkicked: return "intentional nonkicked";
    case e_TouchType_Accidental: return "accidental";
    default: return "other";
  }
}

}  // namespace

TEST_CASE("snapshot touch inference miss attribution shadow",
          "[sim][observation][touch]") {
  using football::sim::event::RecordedTouch;
  const auto home = football::app::fixtures::MakeDefaultHomeTeam();
  const auto away = football::app::fixtures::MakeDefaultAwayTeam();
  Simulation simulation;
  simulation.Init(home, away, football::model::Pitch{}, {});
  football::ai::DefaultAI ai(home, away);
  const auto& animations = SimulationAccess::AnimationLibraryOf(simulation);
  const SnapshotMetadata& metadata = simulation.SnapshotMetadata();

  std::map<int, int> ground_truth;
  std::map<int, int> matched_by_source;
  std::map<int, int> missed_by_source;
  for (int type : {static_cast<int>(e_TouchType_Intentional_Kicked),
                   static_cast<int>(e_TouchType_Intentional_Nonkicked),
                   static_cast<int>(e_TouchType_Accidental)}) {
    ground_truth[type] = 0;
    matched_by_source[type] = 0;
    missed_by_source[type] = 0;
  }
  std::map<std::string, int> reasons;
  for (const char* reason : {"snapshot discontinuity", "inactive slot",
                             "clip changed", "frame rewound/reset",
                             "spatial gate", "no baked touch crossing", "other"}) {
    reasons[reason] = 0;
  }
  std::size_t matched = 0, missed = 0, spurious = 0;

  int step = 0;
  for (; step < 4000; ++step) {
    const auto* latest = simulation.Snapshots().Latest();
    REQUIRE(latest != nullptr);
    const SnapshotRecord previous = *latest;

    PlayerControlSet controls;
    ai.Update(simulation.Observe(), controls);
    simulation.Step(controls);

    const auto* after = simulation.Snapshots().Latest();
    REQUIRE(after != nullptr);
    const SnapshotRecord current = *after;

    // Ground truth from the accepted AcceptedTouch log, keyed by the executed
    // step the snapshot records (never the possibly-repeating timeline tick).
    std::vector<RecordedTouch> step_touches;
    for (const RecordedTouch& touch : SimulationAccess::RecordedTouchesOf(simulation)) {
      if (touch.step_index == current.stamp.step_index) step_touches.push_back(touch);
    }

    const std::vector<TouchCandidate> inferred =
        InferTouches(previous, current, metadata, animations);

    const bool discontinuity = previous.stamp.generation != current.stamp.generation ||
        current.stamp.step_index != previous.stamp.step_index + 1 ||
        previous.snapshot.players.size() != current.snapshot.players.size() ||
        metadata.player_ids.size() != current.snapshot.players.size();

    std::vector<bool> used(inferred.size(), false);
    for (const RecordedTouch& touch : step_touches) {
      ++ground_truth[static_cast<int>(touch.type)];
      bool touch_matched = false;
      for (std::size_t i = 0; i < inferred.size(); ++i) {
        if (!used[i] && inferred[i].player == touch.player) {
          used[i] = true;
          touch_matched = true;
          break;
        }
      }
      if (touch_matched) {
        ++matched;
        ++matched_by_source[static_cast<int>(touch.type)];
        continue;
      }
      ++missed;
      ++missed_by_source[static_cast<int>(touch.type)];

      int slot = -1;
      for (std::size_t i = 0; i < metadata.player_ids.size(); ++i) {
        if (metadata.player_ids[i] == touch.player) { slot = static_cast<int>(i); break; }
      }
      if (slot < 0) { ++reasons["other"]; continue; }

      const auto& prev = previous.snapshot.players[slot];
      const auto& cur = current.snapshot.players[slot];
      const bool inactive = !prev.active || !cur.active;
      const bool clip_changed = !inactive && prev.animation_id != cur.animation_id;
      const bool frame_rewound = !inactive && !clip_changed && cur.frame < prev.frame;
      bool crossed = false;
      if (!inactive && !clip_changed && prev.animation_id >= 0 &&
          static_cast<std::size_t>(prev.animation_id) < animations.Size()) {
        const AnimationClip& clip = animations.Get(static_cast<std::uint32_t>(prev.animation_id));
        crossed = TestCrossedTouchFrame(prev, cur, clip);
      }
      const bool spatial = crossed &&
          (current.snapshot.ball.position - cur.position).GetLength() > 3.0f;

      if (discontinuity) { ++reasons["snapshot discontinuity"]; }
      else if (inactive) { ++reasons["inactive slot"]; }
      else if (clip_changed) { ++reasons["clip changed"]; }
      else if (frame_rewound) { ++reasons["frame rewound/reset"]; }
      else if (spatial) { ++reasons["spatial gate"]; }
      else if (!crossed) { ++reasons["no baked touch crossing"]; }
      else { ++reasons["other"]; }
    }
    for (std::size_t i = 0; i < inferred.size(); ++i) {
      if (!used[i]) ++spurious;
    }
  }

  std::ostringstream report;
  report << "Shadow: 4000 executed steps\n";
  report << "Ground truth:\n";
  for (const auto& [type, count] : ground_truth) {
    report << "  " << SourceName(static_cast<e_TouchType>(type)) << ": " << count << "\n";
  }
  report << "Inference: matched=" << matched << " missed=" << missed
         << " spurious=" << spurious << "\n";
  report << "Matched by source:\n";
  for (const auto& [type, count] : matched_by_source) {
    report << "  " << SourceName(static_cast<e_TouchType>(type)) << ": " << count << "\n";
  }
  report << "Missed by source:\n";
  for (const auto& [type, count] : missed_by_source) {
    report << "  " << SourceName(static_cast<e_TouchType>(type)) << ": " << count << "\n";
  }
  report << "Miss diagnosis:\n";
  for (const auto& [reason, count] : reasons) {
    report << "  " << reason << ": " << count << "\n";
  }
  report << "window: previous+current only (no future frame)";
  WARN(report.str());
  REQUIRE(static_cast<std::size_t>(step) == 4000);
}
