#ifndef FOOTBALL_SIM_FACT_SIMULATION_FACT_HPP
#define FOOTBALL_SIM_FACT_SIMULATION_FACT_HPP

#include <cstdint>
#include <variant>

#include "foundation/math/vector3.hpp"
#include "foundation/time/tick.hpp"
#include "model/football_types.hpp"
#include "model/player.hpp"
#include "model/team.hpp"
#include "sim/event/touch_type.hpp"

namespace football::sim::event {

// Immutable "this already happened" records produced by physics and actors.
// They carry caller identities and world values, never live actor pointers, so a
// consumer may resolve them against whatever state is current at consumption.
// Facts describe matter of fact; whether a fact is a foul, an offside or a goal
// is a Ruling decision owned by the referee.
//
// Rule-relevant evidence is captured when the fact is produced, from the live
// state at that instant. This keeps the referee's decisions independent of later
// mutations and is what allows consumption to move to a different boundary.

struct BallTouchFact {
  // Actor-local touch instant. In product execution it equals the buffer tick;
  // diagnostic publications may deliberately differ.
  football::sim::Tick touched_at{};
  model::PlayerId player = model::kInvalidPlayerId;
  model::TeamSide team = model::TeamSide::Home;
  e_TouchType type = e_TouchType_None;
  blunted::Vector3 ball_position;
  blunted::Vector3 ball_velocity;
  // Accepted action at the contact instant (e_FunctionType), for event
  // recognition. Defaulted so diagnostics may omit it.
  int action_type = 0;
};



using SimulationFact = std::variant<BallTouchFact>;

// Identity, instant and order of one fact. sequence is the deterministic order
// within its tick; generation rejects facts produced before a situation reset.
struct StampedFact {
  Tick tick{};
  std::uint64_t generation = 0;
  std::uint32_t sequence = 0;
  SimulationFact fact;
};

}  // namespace football::sim::event

#endif  // FOOTBALL_SIM_FACT_SIMULATION_FACT_HPP
