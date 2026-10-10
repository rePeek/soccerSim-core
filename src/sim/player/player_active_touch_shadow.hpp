#ifndef FOOTBALL_PLAYER_ACTIVE_TOUCH_SHADOW_HPP
#define FOOTBALL_PLAYER_ACTIVE_TOUCH_SHADOW_HPP
#include <array>
#include <optional>
#include <vector>
#include <algorithm>
#include <cmath>
#include "football/ball/ball_impulse.hpp"
#include "football/ball/ball_response.hpp"
#include "model/player.hpp"
#include "sim/player/player_action.hpp"
#include "sim/player/player_body_collider_motion.hpp"
#include "sim/event/touch_type.hpp"
#include "football/ball/ball.hpp"
#include "sim/player/player_active_touch_model.hpp"
#include "foundation/time/tick.hpp"

// Diagnostic stages, NOT another football action enum.
enum class ActiveTouchOrigin { Scheduled, Controlled, RetainAcquire, RetainAnchor, RetainRelease };
enum class ActiveTouchStage { Pending, Rejected, Executed, NoImpulse, Constraint };
enum class ActiveTouchReject { None, Authorization, Distance, Height, Unsupported };
struct ActiveTouchCandidate {
  football::model::PlayerId player = football::model::kInvalidPlayerId;
  PlayerBodyPart body_part = PlayerBodyPart::LowerBody;
  e_FunctionType action = e_FunctionType_None;
  blunted::Vector3 target_velocity = blunted::Vector3(0);
  blunted::Vector3 target_angular_velocity = blunted::Vector3(0);
  blunted::Vector3 contact_point = blunted::Vector3(0);
  football::sim::Tick scheduled_tick; // physical endpoint; old rule stamp is separate
  float reach_error = 0;
  bool reachable = false;
};
struct ActiveTouchObservation {
  ActiveTouchCandidate candidate;
  ActiveTouchOrigin origin = ActiveTouchOrigin::Scheduled;
  ActiveTouchStage stage = ActiveTouchStage::Pending;
  ActiveTouchReject rejection = ActiveTouchReject::None;
  football::ball::BallState before, after;
  std::optional<football::ball::BallState> passive_endpoint;
  bool same_part_passive_impact = false, other_player_passive_impact = false;
  blunted::Vector3 raw_animation_point = blunted::Vector3(0); // desired BALL CENTER, not surface
  blunted::Vector3 player_position = blunted::Vector3(0);
  football::sim::Tick legacy_rule_tick;
  int animation_id = -1, contact_frame = -1, elapsed = 0;
  e_TouchType accepted_type = e_TouchType_None;
  bool incoming_retain_override = false;
  // Set only when the legacy contact frame actually evaluated reachability
  // (may_touch was true), so the P5c model reconciliation is not confounded by
  // authorization rejections or pending frames.
  bool legacy_reach_evaluated = false;
};
class ActiveTouchShadowSink {
 public:
  virtual ~ActiveTouchShadowSink() = default;
  virtual void Observe(const ActiveTouchObservation&) = 0;
};
struct ActiveTouchComparison {
  ActiveTouchObservation observation;
  std::optional<football::ball::BallImpulse> impulse;
  football::ball::BallState response;
  float velocity_error = 0, spin_error = 0, surface_error = 0, raw_center_distance = 0;
  bool fallback_point = false;
};
// The baked touch position is a desired ball center. Project towards it onto the
// current ball surface. Coincident centers require an explicit foot/body proxy.
// This provisional proxy is reported; it is NOT proof of a posed foot contact.
inline ActiveTouchComparison CompareActiveTouch(ActiveTouchObservation observation,
    const football::model::BallConfig& config) {
  ActiveTouchComparison out;
  out.observation = observation;
  const auto& endpoint = observation.passive_endpoint ? *observation.passive_endpoint : observation.before;
  out.response = endpoint;
  if (observation.stage != ActiveTouchStage::Executed || !observation.candidate.reachable ||
      (observation.origin != ActiveTouchOrigin::Scheduled && observation.origin != ActiveTouchOrigin::Controlled)) return out;
  auto radial = observation.raw_animation_point - endpoint.position;
  out.raw_center_distance = radial.GetLength();
  if (radial.GetLength() < 1e-5f) {
    out.fallback_point = true;
    radial = observation.player_position + blunted::Vector3(0, 0, .1f) - endpoint.position;
  }
  const auto point = endpoint.position + radial.GetNormalized({1, 0, 0}) * config.radius();
  out.observation.candidate.contact_point = point;
  out.impulse = football::ball::BallImpulse{
      (observation.candidate.target_velocity - endpoint.velocity) * config.mass(), point};
  out.response = football::ball::ApplyImpulseAtPoint(endpoint, out.impulse->impulse, point, config);
  out.velocity_error = (out.response.velocity - observation.candidate.target_velocity).GetLength();
  out.spin_error = (out.response.angular_velocity - observation.candidate.target_angular_velocity).GetLength();
  out.surface_error = std::fabs((point - endpoint.position).GetLength() - config.radius());
  return out;
}
struct ActiveTouchActionReport {
  std::uint64_t pending_samples = 0, frames = 0, executed = 0, no_impulse = 0;
  std::array<std::uint64_t, 5> rejected{};
  std::uint64_t fallback_points = 0, position_mutations = 0;
  std::uint64_t passive_endpoints = 0, same_part_conflicts = 0, other_player_conflicts = 0;
  double velocity_error_sum = 0, spin_error_sum = 0;
  float velocity_error_max = 0, spin_error_max = 0, surface_error_max = 0, spin_max = 0;
  // P5c-2 calibration: flight deviation of the centre-strike physical response
  // from the legacy post-touch state, sampled at 0.5 s / 1 s / 2 s. Largest
  // deviation per action, in metres.
  std::uint64_t trajectory_samples = 0;
  float trajectory_deviation_50 = 0, trajectory_deviation_100 = 0,
        trajectory_deviation_200 = 0;
};
struct ActiveTouchShadowReport {
  std::array<ActiveTouchActionReport, e_FunctionType_Special + 1> actions{};
  std::array<std::uint64_t, 5> origins{};
  std::uint64_t duplicate_candidates = 0, contending_ticks = 0, dropped_details = 0;
  // P5c-1/2 reconciliation: the pure ProposeActiveTouch model is run on the
  // same captured inputs and its reach decision is compared with the legacy
  // contact-frame reach gate. Disagreements are a scheduling/source mismatch,
  // not a physics error.
  std::uint64_t model_reach_checks = 0, model_reach_disagreements = 0;
  // Largest |legacy reach_error - model reach_error| over disagreements; the
  // standard position source (legacy Predict(0) cache vs endpoint state) is the
  // expected cause, so this bounds how far apart the two sources are.
  float model_reach_gap_max = 0.0f;
  // Only the last tick is retained. Aggregates do not grow with match length.
  std::vector<ActiveTouchComparison> latest;
  std::uint64_t step = 0, generation = 0;
  void Record(ActiveTouchObservation observation, std::uint64_t step_index,
              std::uint64_t reset_generation, const football::model::BallConfig& config,
              football::ball::Ball* trajectory_ball = nullptr) {
    if (step != step_index || generation != reset_generation) {
      latest.clear(); step = step_index; generation = reset_generation;
    }
    auto comparison = CompareActiveTouch(observation, config);
    if (observation.legacy_reach_evaluated) {
      football::sim::player::TouchProposalInput proposal;
      proposal.passive_endpoint = observation.passive_endpoint
                                        ? *observation.passive_endpoint
                                        : observation.before;
      proposal.desired_ball_center = observation.raw_animation_point;
      proposal.target_velocity = observation.candidate.target_velocity;
      proposal.config = config;
      proposal.reach = observation.incoming_retain_override ? 1.0f : 0.4f;
      const auto model = football::sim::player::ProposeActiveTouch(proposal);
      ++model_reach_checks;
      if (model.reachable != observation.candidate.reachable) {
        ++model_reach_disagreements;
        model_reach_gap_max = std::max(model_reach_gap_max,
            std::fabs(model.reach_error - observation.candidate.reach_error));
      }
    }
    auto& a = actions.at(static_cast<std::size_t>(observation.candidate.action));
    ++origins.at(static_cast<std::size_t>(observation.origin));
    if (observation.stage == ActiveTouchStage::Pending) ++a.pending_samples;
    else if (observation.origin == ActiveTouchOrigin::Scheduled) {
      ++a.frames;
      if (observation.stage == ActiveTouchStage::Rejected) ++a.rejected.at(static_cast<std::size_t>(observation.rejection));
      if (observation.stage == ActiveTouchStage::NoImpulse) ++a.no_impulse;
    }
    if (comparison.impulse) {
      ++a.executed; a.fallback_points += comparison.fallback_point;
      a.position_mutations += (observation.before.position - observation.after.position).GetLength() > 1e-6f;
      a.passive_endpoints += observation.passive_endpoint.has_value();
      a.same_part_conflicts += observation.same_part_passive_impact;
      a.other_player_conflicts += observation.other_player_passive_impact;
      a.velocity_error_sum += comparison.velocity_error; a.spin_error_sum += comparison.spin_error;
      a.velocity_error_max = std::max(a.velocity_error_max, comparison.velocity_error);
      a.spin_error_max = std::max(a.spin_error_max, comparison.spin_error);
      a.surface_error_max = std::max(a.surface_error_max, comparison.surface_error);
      a.spin_max = std::max(a.spin_max, comparison.response.angular_velocity.GetLength());
      std::size_t prior = 0;
      for (const auto& item : latest) if (item.impulse) {
        ++prior;
        duplicate_candidates += item.observation.candidate.player == observation.candidate.player &&
            item.observation.animation_id == observation.animation_id && item.observation.origin == observation.origin;
      }
      if (prior == 1) ++contending_ticks;
      if (trajectory_ball) {
        // P5c-2 calibration only: compare the physical centre-strike response
        // with the legacy post-touch state using the same production kernel.
        const auto deviation = [&](football::sim::TickSpan horizon) {
          trajectory_ball->Reset(observation.after);
          const auto legacy =
              trajectory_ball->Predict(horizon, football::ball::BallEnvironment{});
          trajectory_ball->Reset(comparison.response);
          const auto physical =
              trajectory_ball->Predict(horizon, football::ball::BallEnvironment{});
          return (legacy.position - physical.position).GetLength();
        };
        ++a.trajectory_samples;
        a.trajectory_deviation_50 =
            std::max(a.trajectory_deviation_50, deviation(football::sim::TickSpan{50}));
        a.trajectory_deviation_100 =
            std::max(a.trajectory_deviation_100, deviation(football::sim::TickSpan{100}));
        a.trajectory_deviation_200 =
            std::max(a.trajectory_deviation_200, deviation(football::sim::TickSpan{200}));
      }
    }
    if (latest.size() < latest.capacity()) latest.push_back(std::move(comparison));
    else ++dropped_details;
  }
};
#endif
