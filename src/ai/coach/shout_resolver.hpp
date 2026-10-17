#ifndef FOOTBALL_AI_COACH_SHOUT_RESOLVER_HPP
#define FOOTBALL_AI_COACH_SHOUT_RESOLVER_HPP

#include <cstdint>

#include "football/ai/shout.hpp"

namespace football::ai {

// Deterministic shout -> mental-effect mapping. v1 deliberately has no player
// personality model: the same shout produces the same bounded effect on
// everyone. Varying it per player is deferred until the observation layer can
// supply real emotional/personality data.
class ShoutResolver {
 public:
  static PlayerMentalEffect EffectFor(ShoutType type);
  static std::uint64_t DurationTicks(ShoutType type);
};

}  // namespace football::ai

#endif  // FOOTBALL_AI_COACH_SHOUT_RESOLVER_HPP
