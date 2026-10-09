#include <algorithm>
#include <limits>
#include <stdexcept>
#include <variant>

#include <catch2/catch_test_macros.hpp>

#include "sim/event/event_recognizer.hpp"
#include "sim/observation/temporal_graph.hpp"

namespace {
using namespace football::sim;
using namespace football::sim::observation;
namespace event = football::sim::event;

SnapshotRecord Frame(std::uint64_t step, Tick tick, std::uint64_t generation, std::size_t players = 2) {
  SnapshotRecord record;
  record.stamp = {step, tick, generation};
  record.snapshot.ball.position = {static_cast<float>(step * 2), 0, 0};
  record.snapshot.ball.velocity = {static_cast<float>(step), 0, 0};
  record.snapshot.ball.angular_velocity = {1, 2, 3};
  record.snapshot.players.resize(players);
  for (std::size_t i = 0; i < players; ++i) {
    auto& p = record.snapshot.players[i];
    p.position = record.snapshot.ball.position + blunted::Vector3(3 + static_cast<float>(i) * 30, 0, 0);
    p.velocity = {1, 2, 0};
    p.facing = {0, 1, 0};
    p.body_facing = {-1, 0, 0};
    p.animation_id = 9;
    p.frame = static_cast<std::uint32_t>(step);
    p.active = true;
    p.has_possession = i == 0;
  }
  return record;
}
event::StampedFact Touch(Tick tick, std::uint64_t generation, int action) {
  event::StampedFact fact;
  fact.tick = tick;
  fact.generation = generation;
  event::BallTouchFact touch;
  touch.touched_at = tick;
  touch.player = 7;
  touch.team = football::model::TeamSide::Home;
  touch.action_type = action;
  // Exact contact evidence intentionally differs from whole-step snapshots.
  touch.ball_position = {-100, 20, 0};
  fact.fact = touch;
  return fact;
}
std::size_t Edges(const TemporalGraph& graph, TemporalEdgeKind kind) {
  return static_cast<std::size_t>(std::count_if(graph.edges.begin(), graph.edges.end(),
      [kind](const auto& edge) { return edge.kind == kind; }));
}
}

TEST_CASE("graph features identity slots spatial relations and temporal edges use snapshots only", "[snapshot][graph]") {
  SnapshotMetadata metadata{{1001, 4000000000U}};
  SnapshotHistory history(3);
  history.Reset(2);
  for (std::uint64_t step = 0; step < 5; ++step) {
    const auto record = Frame(step, Tick{step}, 0);
    history.Record(record.stamp, record.snapshot);
  }
  const auto built = GraphBuilder::BuildLatest(history, metadata, 3);
  REQUIRE(built.has_value());
  const auto& graph = *built;
  REQUIRE(graph.frames.size() == 3);
  REQUIRE(graph.frames.front().step_index == 2);
  REQUIRE(graph.nodes.size() == 9);
  REQUIRE(Edges(graph, TemporalEdgeKind::Spatial) == 6); // ball <-> near player only
  REQUIRE(Edges(graph, TemporalEdgeKind::Temporal) == 6); // three slots × two intervals
  for (std::size_t f = 0; f < 3; ++f) {
    const auto expected = Frame(f + 2, Tick{f + 2}, 0);
    const auto& ball = graph.nodes[f * 3];
    REQUIRE_FALSE(ball.player_id);
    REQUIRE(ball.active);
    REQUIRE(ball.frame_index == f);
    REQUIRE(ball.position == expected.snapshot.ball.position);
    REQUIRE(ball.velocity == expected.snapshot.ball.velocity);
    REQUIRE(ball.angular_velocity == expected.snapshot.ball.angular_velocity);
    for (std::size_t p = 0; p < 2; ++p) {
      const auto& node = graph.nodes[f * 3 + 1 + p];
      const auto& player = expected.snapshot.players[p];
      REQUIRE(node.player_id == metadata.player_ids[p]);
      REQUIRE(node.position == player.position);
      REQUIRE(node.velocity == player.velocity);
      REQUIRE(node.facing == player.facing);
      REQUIRE(node.body_facing == player.body_facing);
      REQUIRE(node.animation_id == 9);
      REQUIRE(node.animation_frame == f + 2);
      REQUIRE(node.has_possession == (p == 0));
    }
  }
  for (const auto& edge : graph.edges) {
    REQUIRE(edge.displacement == graph.nodes[edge.target].position - graph.nodes[edge.source].position);
    if (edge.kind == TemporalEdgeKind::Temporal) {
      REQUIRE(graph.nodes[edge.source].player_id == graph.nodes[edge.target].player_id);
      REQUIRE(graph.nodes[edge.target].frame_index == graph.nodes[edge.source].frame_index + 1);
      REQUIRE(edge.elapsed == TickSpan{1});
    } else REQUIRE(edge.elapsed == TickSpan{});
  }
  REQUIRE_FALSE(GraphBuilder::BuildLatest(history, metadata, 4));
  history.Clear();
  REQUIRE(graph.nodes.front().position == blunted::Vector3(4, 0, 0)); // owning result
}

TEST_CASE("graph never links inactive actors or reset missing-step missing-tick duplicate-tick boundaries", "[snapshot][graph]") {
  const SnapshotMetadata metadata{{10, 20}};
  for (const SnapshotStamp boundary : {SnapshotStamp{2, Tick{2}, 1}, {3, Tick{2}, 0},
                                       {2, Tick{100}, 0}, {2, Tick{1}, 0}}) {
    std::vector<SnapshotRecord> records{Frame(1, Tick{1}, 0), Frame(boundary.step_index, boundary.timeline_tick, boundary.generation)};
    const auto graph = GraphBuilder::Build(records, metadata);
    REQUIRE(Edges(graph, TemporalEdgeKind::Temporal) == 0);
  }
  auto first = Frame(1, Tick{1}, 0);
  auto second = Frame(2, Tick{2}, 0);
  first.snapshot.players[0].active = false;
  const std::vector<SnapshotRecord> records{first, second};
  const auto graph = GraphBuilder::Build(records, metadata);
  REQUIRE(graph.nodes.size() == 6);
  REQUIRE(graph.nodes[1].player_id == 10);
  REQUIRE_FALSE(graph.nodes[1].active);
  REQUIRE(Edges(graph, TemporalEdgeKind::Spatial) == 2);
  REQUIRE(Edges(graph, TemporalEdgeKind::Temporal) == 2);
  for (const auto& edge : graph.edges) { REQUIRE(edge.source != 1); REQUIRE(edge.target != 1); }
  const auto empty = GraphBuilder::Build({}, metadata);
  REQUIRE(empty.nodes.empty());
  REQUIRE(empty.edges.empty());
}

TEST_CASE("graph rejects malformed value windows without live fallbacks", "[snapshot][graph]") {
  const SnapshotMetadata metadata{{10, 20}};
  std::vector<SnapshotRecord> records{Frame(1, Tick{1}, 0), Frame(2, Tick{2}, 0)};
  REQUIRE_THROWS_AS(GraphBuilder::Build(records, metadata, {0}), std::invalid_argument);
  REQUIRE_THROWS_AS(GraphBuilder::Build(records, metadata, {std::numeric_limits<float>::infinity()}), std::invalid_argument);
  REQUIRE_THROWS_AS(GraphBuilder::Build(records, SnapshotMetadata{{10, 10}}), std::invalid_argument);
  REQUIRE_THROWS_AS(GraphBuilder::Build(records, SnapshotMetadata{{football::model::kInvalidPlayerId, 20}}), std::invalid_argument);
  REQUIRE_THROWS_AS(GraphBuilder::Build(records, SnapshotMetadata{{10}}), std::invalid_argument);
  records[1].stamp.step_index = 1;
  REQUIRE_THROWS_AS(GraphBuilder::Build(records, metadata), std::invalid_argument);
  records[1].stamp = {2, Tick{}, 0};
  REQUIRE_THROWS_AS(GraphBuilder::Build(records, metadata), std::invalid_argument);
  records[1].stamp = {2, Tick{2}, 0};
  records[0].stamp.generation = 1;
  REQUIRE_THROWS_AS(GraphBuilder::Build(records, metadata), std::invalid_argument);
  records[0].stamp.generation = 0;
  records[1].snapshot.ball.position.coords[0] = std::numeric_limits<float>::quiet_NaN();
  REQUIRE_THROWS_AS(GraphBuilder::Build(records, metadata), std::invalid_argument);
}

TEST_CASE("Event trajectory is published after resolution-step snapshot and cannot change contact transitions", "[snapshot][event]") {
  SnapshotHistory history(10);
  history.Reset(0);
  event::EventRecognizer recognizer;
  recognizer.Consume(Touch(Tick{10}, 7, e_FunctionType_ShortPass), {true, false, 1});
  REQUIRE(recognizer.HasActivePass());
  const auto id = std::get<event::PassStarted>(recognizer.transitions().front()).id;
  for (std::uint64_t step = 1; step < 4; ++step) {
    const auto frame = Frame(step, Tick{10 + step}, 7, 0);
    history.Record(frame.stamp, frame.snapshot);
    recognizer.PublishTrajectories(history);
    REQUIRE(recognizer.trajectories().empty());
  }
  recognizer.Consume(Touch(Tick{13}, 7, e_FunctionType_BallControl), {true, false, 4});
  const auto transition = std::get<event::PassEnded>(recognizer.transitions().front());
  REQUIRE(transition.status == event::EventStatus::Completed);
  REQUIRE(recognizer.trajectories().empty());
  const auto end = Frame(4, Tick{14}, 7, 0);
  history.Record(end.stamp, end.snapshot);
  recognizer.PublishTrajectories(history);
  REQUIRE(recognizer.trajectories().size() == 1);
  const auto summary = recognizer.trajectories().front();
  REQUIRE(summary.event == id);
  REQUIRE(summary.generation == 7);
  REQUIRE(summary.first_step == 1);
  REQUIRE(summary.last_step == 4);
  REQUIRE(summary.actor == 7);
  REQUIRE(summary.kind == event::TrajectoryKind::Pass);
  REQUIRE(summary.team == football::model::TeamSide::Home);
  REQUIRE(summary.sample_count == 4);
  REQUIRE(summary.complete);
  REQUIRE(summary.first_ball_position == blunted::Vector3(2, 0, 0));
  REQUIRE(summary.last_ball_position == blunted::Vector3(8, 0, 0));
  REQUIRE(summary.ball_path_length == 6);
  REQUIRE(summary.peak_ball_speed == 4);
  REQUIRE(std::get<event::PassEnded>(recognizer.transitions().front()).receiver == transition.receiver);
  history.Clear();
  REQUIRE(summary.last_ball_position == blunted::Vector3(8, 0, 0));
  recognizer.PublishTrajectories(history);
  REQUIRE(recognizer.trajectories().empty());
}

TEST_CASE("Event windows mark overwritten gaps and resets incomplete without inventing movement", "[snapshot][event]") {
  for (int scenario = 0; scenario < 4; ++scenario) {
    SnapshotHistory history(scenario == 0 ? 2 : 10);
    history.Reset(0);
    event::EventRecognizer recognizer;
    recognizer.Consume(Touch(Tick{10}, 7, e_FunctionType_ShortPass), {true, false, 1});
    for (std::uint64_t step = 1; step <= 4; ++step) {
      if (scenario == 1 && step == 2) continue;
      const auto frame = Frame(step, Tick{10 + step + (scenario == 2 && step > 1 ? 100 : 0)},
          scenario == 3 && step > 1 ? 8 : 7, 0);
      history.Record(frame.stamp, frame.snapshot);
    }
    recognizer.Consume(Touch(Tick{13}, 7, e_FunctionType_BallControl), {true, false, 4});
    recognizer.PublishTrajectories(history);
    REQUIRE(recognizer.trajectories().size() == 1);
    const auto& summary = recognizer.trajectories().front();
    REQUIRE_FALSE(summary.complete);
    REQUIRE(summary.ball_path_length == (scenario == 0 || scenario == 1 ? 2 : scenario == 2 ? 4 : 0));
    REQUIRE(summary.sample_count == (scenario == 0 ? 2 : scenario == 1 ? 3 : scenario == 2 ? 4 : 1));
  }
}

TEST_CASE("shot goals cancellations and diagnostic facts keep independent semantic behavior", "[snapshot][event]") {
  SnapshotHistory history(10);
  history.Reset(0);
  const auto frame = Frame(1, Tick{1}, 0, 0);
  history.Record(frame.stamp, frame.snapshot);
  event::EventRecognizer recognizer;
  recognizer.Consume(Touch(Tick{}, 0, e_FunctionType_Shot), {true, false, 1});
  recognizer.OnGoalConfirmed(Tick{1}, football::model::TeamSide::Home, 1);
  REQUIRE(std::get<event::ShotEnded>(recognizer.transitions().front()).goal);
  recognizer.PublishTrajectories(history);
  REQUIRE(recognizer.trajectories().size() == 1);
  REQUIRE(recognizer.trajectories().front().complete);
  REQUIRE(recognizer.trajectories().front().sample_count == 1);
  REQUIRE(recognizer.trajectories().front().ball_path_length == 0);
  recognizer.Reset();
  recognizer.Consume(Touch(Tick{}, 0, e_FunctionType_ShortPass), {true, false, 1});
  recognizer.Advance(Tick{1}, {false, false, 1});
  REQUIRE(std::get<event::PassEnded>(recognizer.transitions().front()).status == event::EventStatus::Cancelled);
  recognizer.PublishTrajectories(history);
  REQUIRE(recognizer.trajectories().size() == 1);
  recognizer.Reset();
  recognizer.Consume(Touch(Tick{}, 0, e_FunctionType_Shot), {true, false}); // outside Step
  recognizer.OnGoalConfirmed(Tick{1}, football::model::TeamSide::Home);
  recognizer.PublishTrajectories(history);
  REQUIRE(recognizer.trajectories().empty());
  REQUIRE(std::get<event::ShotEnded>(recognizer.transitions().front()).goal);
}
