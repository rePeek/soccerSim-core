#ifndef FOOTBALL_AI_DEFAULT_DECISIONS_HPP
#define FOOTBALL_AI_DEFAULT_DECISIONS_HPP

#include <span>

#include "football/ai/tactical_board.hpp"

struct WorldState;
class PlayerControlSet;

namespace football::ai {

// The Default policy decision body. One implementation shared by the public
// DefaultAI baseline and the internal DefaultPolicy adapter, so the two can
// never diverge. It fully replaces the tick's output and never mutates the
// world or the boards.
void ComputeDefaultControls(const WorldState& world,
                            std::span<const TacticalBoard> boards,
                            PlayerControlSet& output);

}  // namespace football::ai

#endif  // FOOTBALL_AI_DEFAULT_DECISIONS_HPP
