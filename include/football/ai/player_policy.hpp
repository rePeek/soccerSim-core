#ifndef FOOTBALL_AI_PLAYER_POLICY_HPP
#define FOOTBALL_AI_PLAYER_POLICY_HPP

#include <span>

#include "football/ai/shout.hpp"
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

// One player's transient, bounded mental effect for the tick. Never stored on
// the player entity and never applied to base abilities.
struct PlayerMentalEffectEntry {
  football::sim::TeamSide side = football::sim::TeamSide::Home;
  football::player::PlayerId player = football::player::kInvalidPlayerId;
  PlayerMentalEffect effect;
};

// The Player AI input: the public Observation (WorldState), the coach-owned
// tactical intent (TacticalBoard) and read-only transient mental effects.
// Shouts never enter WorldState, the boards or a global.
struct PlayerAIContext {
  const WorldState* world = nullptr;
  std::span<const TacticalBoard> tactics{};
  std::span<const PlayerMentalEffectEntry> mental_effects{};

  PlayerMentalEffect MentalEffect(football::sim::TeamSide side,
                                  football::player::PlayerId player) const {
    for (const PlayerMentalEffectEntry& entry : mental_effects)
      if (entry.side == side && entry.player == player) return entry.effect;
    return {};
  }
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

  virtual void Update(const PlayerAIContext& context,
                      PlayerControlSet& output) = 0;
};

}  // namespace football::ai

#endif  // FOOTBALL_AI_PLAYER_POLICY_HPP
