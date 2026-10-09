#ifndef FOOTBALL_SIM_EVENT_SIMULATION_FACT_HPP
#define FOOTBALL_SIM_EVENT_SIMULATION_FACT_HPP

#include <cstdint>
#include <variant>

#include "foundation/math/vector3.hpp"
#include "foundation/time/tick.hpp"
#include "model/football_types.hpp"
#include "model/player.hpp"
#include "model/team.hpp"
#include "sim/event/touch_type.hpp"

namespace football::sim::event {

// Immutable "this already happened" records produced by physics/actor modules.
// They carry caller identities and world values, never live actor pointers, so a
// consumer may resolve them against whatever state is current at consumption.
// Facts describe matter of fact; whether a fact is a foul, an offside or a goal
// is a Ruling decision owned by the referee.

struct BallTouchFact {
  model::PlayerId player = model::kInvalidPlayerId;
  model::TeamSide team = model::TeamSide::Home;
  e_TouchType type = e_TouchType_None;
  blunted::Vector3 ball_position;
  blunted::Vector3 ball_velocity;
};

// A contact that made an actor fall or lose their footing. This is a physical
// consequence, not a foul verdict: the referee decides that from the fact.
struct PlayerTripFact {
  model::PlayerId victim = model::kInvalidPlayerId;
  model::PlayerId offender = model::kInvalidPlayerId;
  int trip_type = 0;
  blunted::Vector3 victim_position;
  blunted::Vector3 offender_position;
  blunted::Vector3 ball_position;
};

// The two semantic families the current rules still distinguish. Touchline and
// goal-line checks are state predicates (the ball is outside a line); the goal
// mouth check is a trajectory crossing test. Keep them distinct while the
// legacy out-of-play grace period and restart classification are preserved.
enum class BoundaryKind {
  TouchlineOutside,
  GoalLineOutside,
  GoalMouthCrossed,
};

struct BallBoundaryFact {
  BoundaryKind kind = BoundaryKind::TouchlineOutside;
  int side = 0;
  blunted::Vector3 previous_position;
  blunted::Vector3 current_position;
};

using SimulationFact =
    std::variant<BallTouchFact, PlayerTripFact, BallBoundaryFact>;

// Identity, instant and order of one fact. sequence is the deterministic order
// within its tick; generation rejects facts produced before a situation reset.
struct StampedFact {
  Tick tick{};
  std::uint64_t generation = 0;
  std::uint32_t sequence = 0;
  SimulationFact fact;
};

}  // namespace football::sim::event

#endif  // FOOTBALL_SIM_EVENT_SIMULATION_FACT_HPP
