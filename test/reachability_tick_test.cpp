#include <limits>
#include <optional>
#include <type_traits>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "sim/player/player_locomotion.hpp"
#include "sim/query/reachability.hpp"

namespace {
using namespace football::sim;

// Deliberately no pruning or nested solver calls: execute the physics primitive
// independently for every candidate and test both radii at every rollout tick.
template<class Target>
PlayerLocomotionReach RolloutOracle(PlayerKinematicState start, Target target,
                                    const PlayerLocomotionParameters& parameters,
                                    TickSpan horizon) {
  PlayerLocomotionReach result;
  for (TickSpan candidate{}; candidate <= horizon; candidate += TickSpan{1}) {
    auto state = start;
    const auto point = target(candidate).Get2D();
    for (TickSpan elapsed{}; elapsed <= candidate; elapsed += TickSpan{1}) {
      const float distance = (point - state.position).GetLength();
      if (!result.optimistic && distance <= kLocomotionOptimisticReachRadius)
        result.optimistic = candidate;
      if (!result.usual && distance <= kLocomotionUsualReachRadius)
        result.usual = candidate;
      PlayerLocomotionInput input;
      input.desiredVelocity = (point - state.position).GetNormalized(state.facing) * parameters.maxSpeed;
      input.idleFacing = state.facing;
      PlayerLocomotion::Step(state, input, parameters, kTickSeconds);
    }
    if (result.usual) break;
  }
  return result;
}

static_assert(std::is_same_v<decltype(PlayerLocomotionReach::usual), std::optional<TickSpan>>);

TEST_CASE("Discrete intercept horizons agree with independently executed candidate rollouts", "[sim][tick][reachability]") {
  for (float speed : {0.f, 3.5f, 7.5f}) {
    for (float facing : {0.f, 1.5f, 3.f}) {
      PlayerKinematicState state;
      state.velocity = blunted::Vector3(speed, 0, 0);
      state.facing = blunted::Vector3(1, 0, 0).GetRotated2D(facing);
      PlayerLocomotionParameters parameters;
      for (float target_distance : {0.02f, 0.5f, 2.f, 5.f}) {
        for (float drift : {-2.f, 0.f, 4.f}) {
          const auto target = [=](TickSpan elapsed) {
            return blunted::Vector3(target_distance, drift * ToSeconds(elapsed), 0);
          };
          for (TickSpan horizon : {TickSpan{}, TickSpan{1}, TickSpan{20}, TickSpan{70}}) {
            INFO(speed << " " << facing << " " << target_distance << " " << drift << " " << horizon.value);
            const auto expected = RolloutOracle(state, target, parameters, horizon);
            const auto exact = PlayerLocomotion::EstimateEarliestInterceptExact(
                state, target, parameters, parameters.maxSpeed, horizon,
                kLocomotionUsualReachRadius, kLocomotionOptimisticReachRadius);
            REQUIRE(exact.usual == expected.usual);
            REQUIRE(exact.optimistic == expected.optimistic);
            const auto hybrid = PlayerLocomotion::EstimateEarliestInterceptHybrid(
                state, target, parameters, parameters.maxSpeed, horizon, horizon,
                kLocomotionUsualReachRadius, kLocomotionOptimisticReachRadius, true);
            REQUIRE(hybrid.usual == exact.usual);
            REQUIRE(hybrid.optimistic == exact.optimistic);
          }
        }
      }
    }
  }
}

TEST_CASE("Grid search endpoints and absence never use signed sentinels or wrap", "[sim][tick][reachability]") {
  PlayerKinematicState state;
  PlayerLocomotionParameters parameters;
  const TickSpan huge{std::numeric_limits<std::uint64_t>::max()};
  const auto near = [](TickSpan) { return blunted::Vector3(0); };
  const auto immediate = PlayerLocomotion::EstimateEarliestInterceptExact(
      state, near, parameters, 7.5f, huge, 0.28f, 0.9f);
  REQUIRE(immediate.usual == TickSpan{});
  REQUIRE(immediate.optimistic == TickSpan{});
  REQUIRE_FALSE(PlayerLocomotion::EstimateArrival(state, blunted::Vector3(10, 0, 0),
      parameters, 0.f, huge, 0.28f, 0.9f).usual);
  REQUIRE(PlayerLocomotion::EstimateArrival(state, blunted::Vector3(0),
      parameters, 7.5f, huge, 0.28f, 0.9f).usual == TickSpan{});
  std::vector<TickSpan> visited;
  const auto far = [&](TickSpan at) { visited.push_back(at); return blunted::Vector3(100, 0, 0); };
  const auto bounded = PlayerLocomotion::EstimateEarliestInterceptExact(
      state, far, parameters, 7.5f, TickSpan{2}, 0.28f, 0.9f);
  REQUIRE_FALSE(bounded.usual);
  REQUIRE(visited == std::vector<TickSpan>{TickSpan{}, TickSpan{1}, TickSpan{2}});
  visited.clear();
  const auto split = PlayerLocomotion::EstimateEarliestInterceptHybrid(
      state, far, parameters, 7.5f, TickSpan{2}, TickSpan{1}, 0.28f, 0.9f);
  REQUIRE_FALSE(split.usual);
  REQUIRE(visited == std::vector<TickSpan>{TickSpan{}, TickSpan{1}, TickSpan{2}});
  visited.clear();
  const auto calls = PlayerLocomotionInterceptSolverCalls();
  REQUIRE_THROWS_AS(PlayerLocomotion::EstimateEarliestInterceptHybrid(
      state, far, parameters, 7.5f, TickSpan{1}, TickSpan{2}, 0.28f, 0.9f), std::invalid_argument);
  REQUIRE(visited.empty());
  REQUIRE(PlayerLocomotionInterceptSolverCalls() == calls);
}

TEST_CASE("Continuous arrival values survive comparison with discrete horizons", "[sim][tick][reachability]") {
  PlayerKinematicState state;
  PlayerLocomotionParameters parameters;
  const blunted::Vector3 target(8.815f, 0, 0);
  const int analytic = PlayerLocomotion::EstimateInterceptAnalytic(
      state, target, blunted::Vector3(0), 7.5f, TickSpan{1000}, 0.28f);
  const int steady = PlayerLocomotion::EstimateSteadyReachTime(
      state, target, parameters, 7.5f, TickSpan{1000}, 0.28f);
  for (int estimate : {analytic, steady}) {
    REQUIRE(estimate > 0);
    REQUIRE(estimate % 10 != 0);
  }
  REQUIRE(PlayerLocomotion::EstimateInterceptAnalytic(state, target, blunted::Vector3(0),
      7.5f, TickSpan{static_cast<std::uint64_t>(analytic / 10)}, 0.28f) == -1);
  REQUIRE(PlayerLocomotion::EstimateInterceptAnalytic(state, target, blunted::Vector3(0),
      7.5f, TickSpan{static_cast<std::uint64_t>(analytic / 10 + 1)}, 0.28f) == analytic);
  REQUIRE(PlayerLocomotion::EstimateSteadyReachTime(state, target, parameters,
      7.5f, TickSpan{static_cast<std::uint64_t>(steady / 10)}, 0.28f) == -1);
  REQUIRE(PlayerLocomotion::EstimateSteadyReachTime(state, target, parameters,
      7.5f, TickSpan{static_cast<std::uint64_t>(steady / 10 + 1)}, 0.28f) == steady);
  const TickSpan huge{std::numeric_limits<std::uint64_t>::max()};
  REQUIRE(PlayerLocomotion::EstimateSteadyReachTime(state, target, parameters, 7.5f, huge, 0.28f) == steady);
  const auto unbounded = query::GetTimeNeededForDistance_ms(blunted::Vector3(0), blunted::Vector3(0), target, 7.5f);
  const auto large = query::GetTimeNeededForDistance_ms(blunted::Vector3(0), blunted::Vector3(0), target, 7.5f, false, huge);
  REQUIRE(large.usual_ms == unbounded.usual_ms);
  REQUIRE(large.optimistic_ms == unbounded.optimistic_ms);
  const auto zero = query::GetTimeNeededForDistance_ms(blunted::Vector3(0), blunted::Vector3(0), target, 7.5f, false, TickSpan{});
  REQUIRE(zero.optimistic_ms == 10); // Historical strict > limit, not >=.
}
} // namespace
