#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include "football/contact/player_active_impulse.hpp"
#include "football/contact/player_active_touch_model.hpp"
#include "football/contact/player_action_volume.hpp"
#include "football/contact/player_contact_kernel.hpp"
#include "football/contact/passive_body_touch.hpp"
#include "football/contact/contact_evidence.hpp"

using Catch::Approx;
using football::math::Vector3;
using namespace football::sim::player;
using namespace football::contact;

namespace {
ContactActorSnapshot Actor(float x) {
  ContactActorSnapshot actor;
  actor.position = Vector3(x, 0, 0);
  actor.collider.SetCenter(actor.position);
  actor.facing = Vector3(1, 0, 0);
  actor.direction_vec = Vector3(1, 0, 0);
  actor.physical_balance = 1.0f;
  return actor;
}
}

TEST_CASE("Contact pair is value-only and separates overlapping actors", "[contact]") {
  const auto first = Actor(0);
  const auto second = Actor(0.3f);
  const auto result = ResolveContactPair(first, second, {Vector3(10, 0, 0)});
  REQUIRE(result.first_has_offset);
  REQUIRE(result.second_has_offset);
  CHECK(result.first_offset.coords[0] < 0);
  CHECK(result.second_offset.coords[0] > 0);
  CHECK(result.first_offset.GetLength() <= 0.1f);
  CHECK(result.second_offset.GetLength() <= 0.1f);
  CHECK(first.position == Vector3(0));
  CHECK(second.position == Vector3(0.3f, 0, 0));
  CHECK(first.collider.center == first.position);
}

TEST_CASE("Separated actors produce no pair correction or trip", "[contact]") {
  const auto result = ResolveContactPair(Actor(0), Actor(5), {Vector3(10, 0, 0)});
  CHECK_FALSE(result.first_has_offset);
  CHECK_FALSE(result.second_has_offset);
  CHECK_FALSE(result.first_similar);
  CHECK_FALSE(result.trip_range);
  CHECK_FALSE(result.first_trip.active);
}

TEST_CASE("Ground contact preserves strict tangency semantics", "[contact]") {
  PlayerGroundCollider first, second;
  second.center = Vector3(first.radius + second.radius, 0, 0);
  CHECK_FALSE(first.Intersects(second));
  second.center.coords[0] -= 0.001f;
  CHECK(first.Intersects(second));
}

TEST_CASE("Tackle reach is derived from action phase and kinematics", "[contact]") {
  PlayerActionState action;
  action.type = e_FunctionType_Sliding;
  action.elapsed = football::sim::TickSpan{12};
  PlayerKinematicState state;
  state.position = Vector3(3, 4, 0);
  state.bodyFacing = Vector3(1, 0, 0);
  const auto volume = BuildTackleVolume(action, state);
  REQUIRE(volume.active);
  CHECK(volume.origin == state.position);
  PlayerGroundCollider victim;
  victim.center = state.position + Vector3(1, 0, 0);
  CHECK(volume.Intersects(victim));
  action.elapsed = football::sim::TickSpan{28};
  CHECK_FALSE(BuildTackleVolume(action, state).active);
  action.type = e_FunctionType_Movement;
  action.elapsed = football::sim::TickSpan{12};
  CHECK_FALSE(BuildTackleVolume(action, state).active);
}

TEST_CASE("Active touch proposes an impulse without changing the endpoint", "[contact]") {
  TouchProposalInput input;
  input.passive_endpoint.position = Vector3(0, 0, input.config.radius());
  input.desired_ball_center = input.passive_endpoint.position;
  input.target_velocity = Vector3(8, 0, 0);
  const auto before = input.passive_endpoint;
  const auto proposal = ProposeActiveTouch(input);
  REQUIRE(proposal.reachable);
  CHECK(proposal.rejection == TouchRejection::None);
  CHECK(proposal.velocity_error < 0.00001f);
  CHECK(proposal.surface_error < 0.00001f);
  CHECK(proposal.response.velocity.coords[0] == Approx(8));
  CHECK(input.passive_endpoint.position == before.position);
  CHECK(input.passive_endpoint.velocity == before.velocity);
  CHECK(proposal.spin.GetLength() == Approx(0).margin(0.00001f));
}

TEST_CASE("Active touch rejects unreachable geometry before projection", "[contact]") {
  TouchProposalInput input;
  input.target_velocity = Vector3(8, 0, 0);
  input.strike_radius = 0.08f;
  input.strike_point = Vector3(-5, 0, 0);
  const auto proposal = ProposeActiveTouch(input);
  CHECK_FALSE(proposal.reachable);
  CHECK(proposal.rejection == TouchRejection::Distance);
  input.strike_point = Vector3(-0.15f, 0, 0);
  input.technique.lateral = input.config.radius() * 2;
  CHECK(ProposeActiveTouch(input).rejection == TouchRejection::SurfaceRange);
  input.target_velocity = Vector3(0);
  CHECK(ProposeActiveTouch(input).rejection == TouchRejection::Direction);
}

TEST_CASE("Active impulse arbitration is stable and respects passive ownership", "[contact]") {
  std::array<ActiveImpulseCandidate, 3> candidates;
  candidates[0].player = 5;
  candidates[1].player = 2;
  candidates[2].player = 1;
  for (auto& c : candidates) c.velocity_change_magnitude = 10;
  candidates[2].passive_same_part = true;
  REQUIRE(ArbitrateActiveImpulse(candidates));
  CHECK(ArbitrateActiveImpulse(candidates)->player == 2);
  candidates[0].velocity_change_magnitude = 11;
  CHECK(ArbitrateActiveImpulse(candidates)->player == 5);
  CHECK_FALSE(ArbitrateActiveImpulse({}));
  CHECK(DecideContactAuthority({true, false, true}) == ContactAuthority::PassiveImpact);
  CHECK(DecideContactAuthority({false, false, true}) == ContactAuthority::ActiveTouch);
  CHECK(DecideContactAuthority({false, false, false}) == ContactAuthority::None);
}

TEST_CASE("Standing fall evidence uses thresholds, teams and stable identities", "[contact]") {
  CHECK_FALSE(AttributeStandingFall({1, 0, 1}, {2, 0, 1}));
  CHECK_FALSE(AttributeStandingFall({1, 0, 0.58f}, {2, 1, 0.58f}));
  const auto tie = AttributeStandingFall({2, 0, 0.8f}, {1, 1, 0.8f});
  REQUIRE(tie);
  CHECK_FALSE(tie->victim_is_first);
  const auto reversed = AttributeStandingFall({1, 1, 0.8f}, {2, 0, 0.8f});
  REQUIRE(reversed);
  CHECK(reversed->victim_is_first);
  CHECK(reversed->score == tie->score);
  const auto stronger = AttributeStandingFall({2, 0, 1}, {1, 1, 0.8f});
  REQUIRE(stronger);
  CHECK(stronger->victim_is_first);
  CHECK(stronger->score == Approx(1));
}

TEST_CASE("Sliding severity consumes frozen contact evidence only", "[contact]") {
  football::sim::FoulAssessment fact;
  fact.victim_position = Vector3(1, 0, 0);
  fact.victim_direction = Vector3(1, 0, 0);
  CHECK(SlidingChallengeSeverity(fact) == Approx(2));
  fact.offender_scheduled_contact = true;
  fact.offender_contact_frame = fact.offender_frame = 10;
  fact.offender_contact_position = fact.ball_position;
  CHECK(SlidingChallengeSeverity(fact) == Approx(1));
}

TEST_CASE("Passive body responses accumulate volumes and never add speed", "[contact]") {
  PassiveBodyTouchAccumulator accumulated;
  CHECK_FALSE(FinishPassiveBodyTouches(accumulated, Vector3(10, 0, 0)).velocity);
  for (int i = 0; i < 2; ++i) {
    AccumulatePassiveBodyMotion(accumulated, Vector3(0.1f, 0, 0), Vector3(0), Vector3(0), 1);
    AccumulatePassiveBodyBias(accumulated, Vector3(0.1f, 0, 0), Vector3(0), 0.11f, 0.23f);
  }
  CHECK(accumulated.count == 2);
  const auto response = FinishPassiveBodyTouches(accumulated, Vector3(10, 0, 0));
  REQUIRE(response.velocity);
  CHECK(response.velocity->GetLength() <= 7.0f);
  CHECK(response.rotation_bias == Approx(0.5f));
  const auto still = FinishPassiveBodyTouches(accumulated, Vector3(0));
  REQUIRE(still.velocity);
  CHECK(still.velocity->GetLength() == Approx(0));
}

TEST_CASE("Passive touch reach is supplied by Contact, not the runtime adapter", "[contact]") {
  CHECK(PassiveBodyTouchRadius(e_FunctionType_Movement, false) == Approx(0.04f));
  CHECK(PassiveBodyTouchRadius(e_FunctionType_Movement, true) == Approx(-0.02f));
  CHECK(PassiveBodyTouchRadius(e_FunctionType_Sliding, false) == Approx(0.14f));
  CHECK(PassiveBodyTouchRadius(e_FunctionType_Deflect, false) == Approx(0.24f));
}
