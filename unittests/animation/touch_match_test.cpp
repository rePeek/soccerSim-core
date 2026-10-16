// ANIM-2 A10.4a/A10.4d — deterministic branch coverage for Animation::MatchTouch.
//
// The runtime shadow proves the relocation matches the legacy loop on real
// play; these value-only tests pin each reject branch (including the ones rare
// in a match) with a stub query port.

#include <catch2/catch_test_macros.hpp>

#include <span>
#include <vector>

#include "football/animation/match.hpp"
#include "football/animation/selector.hpp"
#include "football/animation/touch_match.hpp"

using namespace football::math;
using football::animation::MatchTouch;
using football::animation::MotionPlanView;
using football::animation::TouchMatchContext;
using football::animation::TouchMatchQueries;
using football::animation::TouchMatchTrace;
using football::animation::TouchRejectReason;

namespace {

struct StubQueries final : TouchMatchQueries {
  bool has_plan = true;
  Vector3 ball_predict_ticks_pos;
  Vector3 ball_prediction_pos;
  std::vector<Vector3> plan_positions;

  Vector3 BallPredictionAtMs(int) override { return ball_prediction_pos; }
  Vector3 BallPredictAtTicks(int) override { return ball_predict_ticks_pos; }
  float LastTouchBiasAtTick(int) override { return 0.0f; }
  bool MotionPlanFor(AnimationId, MotionPlanView& out) override {
    if (!has_plan) return false;
    out.positions = plan_positions;
    out.outgoing_velocity = Vector3(0);
    out.rotation_offset = 0.0f;
    return true;
  }
};

std::vector<int> CrudeCandidates(const AnimationLibrary& library,
                                 e_FunctionType type, int n) {
  CrudeSelectionQuery query;
  query.byFunctionType = true;
  query.functionType = type;
  DataSet data;
  BakedAnimationSelector::CrudeSelection(library.Clips(), query, data);
  std::vector<int> out;
  for (int i = 0; i < n && i < static_cast<int>(data.size()); ++i) {
    out.push_back(data[i]);
  }
  return out;
}

TouchMatchContext MakeContext(std::span<const int> candidates) {
  TouchMatchContext context;
  context.candidates = candidates;
  context.spatial_angle = 0.0f;
  context.spatial_position = Vector3(0, 0, 0);
  context.incoming_movement = Vector3(0, 0, 0);
  context.desired_movement = Vector3(0, -5, 0);
  context.height_scale = 1.0f;
  context.technical_ballcontrol = 0.5f;
  context.pitch_half_length = 52.5f;
  context.pitch_half_width = 34.0f;
  context.pitch_line_half_width = 0.11f;
  context.touch_offset_ms = 80.0f;
  context.closest_opponent_distance = 5.0f;
  return context;
}

}  // namespace

TEST_CASE("MatchTouch rejects before candidates when somebody else holds the ball") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_BAKED_ANIM_PATH));
  const std::vector<int> candidates = CrudeCandidates(library, e_FunctionType_Trap, 3);
  REQUIRE(!candidates.empty());

  StubQueries queries;
  TouchMatchContext context = MakeContext(candidates);
  context.ball_retained_by_other = true;
  TouchMatchTrace trace;
  const auto result = MatchTouch(library, context, queries, &trace);

  CHECK(trace.rejected_before_candidates);
  CHECK(trace.early_reject == TouchRejectReason::RetainedByOther);
  CHECK(trace.candidates.empty());
  CHECK(result.clip_id == -1);
}

TEST_CASE("MatchTouch reports NoPlan when the motion port has no plan") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_BAKED_ANIM_PATH));
  const std::vector<int> candidates = CrudeCandidates(library, e_FunctionType_Trap, 3);
  REQUIRE(!candidates.empty());

  StubQueries queries;
  queries.has_plan = false;
  TouchMatchTrace trace;
  const auto result = MatchTouch(library, MakeContext(candidates), queries, &trace);

  REQUIRE(trace.candidates.size() == candidates.size());
  for (const auto& candidate : trace.candidates) {
    CHECK(candidate.reject_reason == TouchRejectReason::NoPlan);
  }
  CHECK(result.clip_id == -1);
}

TEST_CASE("MatchTouch rejects candidates whose predicted ball leaves the pitch") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_BAKED_ANIM_PATH));
  const std::vector<int> candidates = CrudeCandidates(library, e_FunctionType_Trap, 3);
  REQUIRE(!candidates.empty());

  StubQueries queries;
  queries.ball_predict_ticks_pos = Vector3(1000.0f, 1000.0f, 0.0f);
  TouchMatchTrace trace;
  const auto result = MatchTouch(library, MakeContext(candidates), queries, &trace);

  REQUIRE(trace.candidates.size() == candidates.size());
  for (const auto& candidate : trace.candidates) {
    CHECK(candidate.reject_reason == TouchRejectReason::OutOfBounds);
  }
  CHECK(result.clip_id == -1);
}

TEST_CASE("MatchTouch rejects a reachable-but-far ball as out of reach") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_BAKED_ANIM_PATH));
  const std::vector<int> candidates = CrudeCandidates(library, e_FunctionType_Trap, 3);
  REQUIRE(!candidates.empty());

  StubQueries queries;
  queries.ball_predict_ticks_pos = Vector3(0.0f, 0.0f, 0.0f);
  // A high ball makes ballDistanceZ exceed the reach threshold.
  queries.ball_prediction_pos = Vector3(0.0f, 0.0f, 5.0f);
  queries.plan_positions.assign(256, Vector3(0, 0, 0));
  TouchMatchTrace trace;
  const auto result = MatchTouch(library, MakeContext(candidates), queries, &trace);

  REQUIRE(!trace.candidates.empty());
  CHECK(trace.candidates.front().reject_reason == TouchRejectReason::OutOfReach);
  CHECK(result.clip_id == -1);
}

TEST_CASE("MatchTouch with no candidates matches nothing") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_BAKED_ANIM_PATH));

  StubQueries queries;
  const std::vector<int> none;
  TouchMatchTrace trace;
  const auto result = MatchTouch(library, MakeContext(none), queries, &trace);
  CHECK(trace.candidates.empty());
  CHECK(result.clip_id == -1);
}

TEST_CASE("MatchTouch returns the same successful selection with or without a trace") {
  // ANIM-2 A11.6/A11.7: the per-candidate trace is optional diagnostics. Asking
  // for it must not change a SUCCESSFUL selection (clip, touch frame/position,
  // smuggle, rotation and the adapted reference motion), and a reused trace must
  // be replaced, not appended, including after an early reject.
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_BAKED_ANIM_PATH));
  const std::vector<int> candidates = CrudeCandidates(library, e_FunctionType_Trap, 3);
  REQUIRE(!candidates.empty());

  StubQueries queries;
  queries.ball_predict_ticks_pos = Vector3(0.0f, 0.0f, 0.0f);
  queries.ball_prediction_pos = Vector3(0.0f, 0.0f, 0.0f);
  queries.plan_positions.assign(256, Vector3(0, 0, 0));
  TouchMatchContext context = MakeContext(candidates);
  // A retained ball makes every reachable candidate win, so this exercises the
  // full successful path rather than only a rejection.
  context.ball_retained_by_player = true;

  const auto without = MatchTouch(library, context, queries);
  REQUIRE(without.clip_id >= 0);

  TouchMatchTrace trace;
  const auto with = MatchTouch(library, context, queries, &trace);

  CHECK(without.clip_id == with.clip_id);
  CHECK(without.touch_frame == with.touch_frame);
  CHECK(without.radius_offset == with.radius_offset);
  CHECK(without.action_smuggle.coords[0] == with.action_smuggle.coords[0]);
  CHECK(without.action_smuggle.coords[1] == with.action_smuggle.coords[1]);
  CHECK(without.action_smuggle.coords[2] == with.action_smuggle.coords[2]);
  CHECK(without.touch_position.coords[0] == with.touch_position.coords[0]);
  CHECK(without.touch_position.coords[1] == with.touch_position.coords[1]);
  CHECK(without.touch_position.coords[2] == with.touch_position.coords[2]);
  REQUIRE(without.positions.size() == with.positions.size());
  for (std::size_t i = 0; i < without.positions.size(); ++i) {
    for (int c = 0; c < 3; ++c) {
      CHECK(without.positions[i].coords[c] == with.positions[i].coords[c]);
    }
  }
  // A successful match stops at the winning candidate, so the trace is a prefix
  // (at least the winner), not the whole candidate list.
  const std::size_t evaluated = trace.candidates.size();
  REQUIRE(evaluated >= 1);
  CHECK(trace.result.clip_id == with.clip_id);

  // Reusing the same trace replaces its diagnostics instead of appending.
  const auto again = MatchTouch(library, context, queries, &trace);
  CHECK(trace.candidates.size() == evaluated);
  CHECK(again.clip_id == with.clip_id);

  // An early reject on the reused trace clears the stale candidate list.
  TouchMatchContext rejected = context;
  rejected.ball_retained_by_other = true;
  const auto rejected_result = MatchTouch(library, rejected, queries, &trace);
  CHECK(trace.rejected_before_candidates);
  CHECK(trace.early_reject == TouchRejectReason::RetainedByOther);
  CHECK(trace.candidates.empty());
  CHECK(rejected_result.clip_id == -1);
}
