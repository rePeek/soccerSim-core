#ifndef FOOTBALL_SIM_EVENT_REFEREE_RULING_HPP
#define FOOTBALL_SIM_EVENT_REFEREE_RULING_HPP

#include <optional>
#include <variant>

#include "foundation/math/vector3.hpp"
#include "foundation/time/tick.hpp"
#include "model/football_types.hpp"
#include "model/player.hpp"
#include "model/team.hpp"

namespace football::sim::event {

// Domain verdicts, distinct from both the facts that triggered them and the
// concrete runtime operations that execute them. Simulation is the only
// executor: the referee decides, it does not mutate other owners.

struct StopPlayRuling {};

struct AwardGoalRuling {
  model::TeamSide team = model::TeamSide::Home;
  std::optional<model::PlayerId> scorer;
};

struct AwardRestartRuling {
  e_GameMode kind = e_GameMode_Normal;
  model::TeamSide team = model::TeamSide::Home;
  blunted::Vector3 position;
};

struct CardRuling {
  model::PlayerId player = model::kInvalidPlayerId;
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
