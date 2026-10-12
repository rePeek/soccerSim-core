#ifndef FOOTBALL_SIM_EVENT_REFEREE_RULING_HPP
#define FOOTBALL_SIM_EVENT_REFEREE_RULING_HPP

#include <optional>
#include <variant>

#include "football/foundation/math/vector3.hpp"
#include "football/foundation/time/tick.hpp"
#include "sim/player/player_role.hpp"
#include "sim/referee/game_mode.hpp"
#include "sim/player/player_definition.hpp"
#include "sim/runtime/match_config.hpp"

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
};

using RefereeRuling =
    std::variant<StopPlayRuling, AwardGoalRuling, AwardRestartRuling, CardRuling>;

// Write-only verdict port. Callers collect rulings and apply them at an explicit
// boundary; the referee never executes them itself.
class RulingSink {
 public:
  virtual ~RulingSink() = default;
  virtual void Submit(const RefereeRuling& ruling) = 0;
};

}  // namespace football::sim::event

#endif  // FOOTBALL_SIM_EVENT_REFEREE_RULING_HPP
