#ifndef FOOTBALL_SIM_EVENT_REFEREE_RULING_HPP
#define FOOTBALL_SIM_EVENT_REFEREE_RULING_HPP

#include <optional>
#include <variant>

#include "football/foundation/math/vector3.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/player/player_role.hpp"
#include "football/referee/game_mode.hpp"
#include "football/player/player_definition.hpp"
#include "football/runtime/match_config.hpp"

namespace football::sim::event {

// Domain verdicts, distinct from both the facts that triggered them and the
// concrete runtime operations that execute them. Simulation is the only
// executor: the referee decides, it does not mutate other owners.

struct StopPlayRuling {};

struct AwardGoalRuling {
  football::sim::TeamSide team = football::sim::TeamSide::Home;
  std::optional<football::player::PlayerId> scorer;
};

struct AwardRestartRuling {
  e_GameMode kind = e_GameMode_Normal;
  football::sim::TeamSide team = football::sim::TeamSide::Home;
  football::math::Vector3 position;
};

struct CardRuling {
  football::player::PlayerId player = football::player::kInvalidPlayerId;
  int type = 0;  // 2 = yellow, 3 = red
  Tick effective_at{};
  // Re-publication of this exact player/type/deadline decision is idempotent.
  bool operator==(const CardRuling&) const = default;
};

using RefereeRuling =
    std::variant<StopPlayRuling, AwardGoalRuling, AwardRestartRuling, CardRuling>;

// Write-only verdict port. Submit only appends; it must never execute/re-enter
// the runtime. The ordered values produced by one referee operation form one
// logical batch, validated completely before the synchronous commit boundary.
class RulingSink {
 public:
  virtual ~RulingSink() = default;
  virtual void Submit(const RefereeRuling& ruling) = 0;
};

}  // namespace football::sim::event

#endif  // FOOTBALL_SIM_EVENT_REFEREE_RULING_HPP
