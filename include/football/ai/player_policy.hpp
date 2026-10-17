#ifndef FOOTBALL_AI_PLAYER_POLICY_HPP
#define FOOTBALL_AI_PLAYER_POLICY_HPP

#include <span>

#include "football/ai/tactical_board.hpp"
#include "football/observation/world_state.hpp"
#include "football/player/player_control_set.hpp"

namespace football::ai {

// Which Player AI implementation drives the roster. New implementations (Eliza,
// a future GNN) select through the same IPlayerPolicy without touching Player,
// Animation or Physics.
enum class PlayerPolicyKind {
  Default,
  Eliza,
  // Coordinated team-aware heuristic: shared attack/defence assignment over
  // the interpreted dual-phase plan. Distinct from the Default baseline.
  Heuristic,
};

// The frozen AI <-> Player protocol.
//
// An implementation reads only the public Observation (WorldState) and the
// coach-owned tactical intent (TacticalBoard), and writes a complete
// PlayerControlSet for this tick. It never holds Player*/Team*/Match*/
// Simulation*, never calls Player::Play/React or Animation::Match, and never
// mutates the world or the boards.
//
// Update() may keep internal per-PlayerId memory. Every call fully replaces
// this tick's output. Output is attributed by PlayerId; a missing player keeps
// the simulation's idle fallback, it is not implicitly carried over.
class IPlayerPolicy {
 public:
  virtual ~IPlayerPolicy() = default;

  // Clears policy-owned memory. Called on Start and by a full state reset.
  virtual void Reset() {}

  virtual void Update(const WorldState& world,
                      std::span<const TacticalBoard, 2> tactics,
                      PlayerControlSet& output) = 0;
};

}  // namespace football::ai

#endif  // FOOTBALL_AI_PLAYER_POLICY_HPP
