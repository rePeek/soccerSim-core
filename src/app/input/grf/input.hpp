#ifndef FOOTBALL_APP_INPUT_GRF_INPUT_HPP
#define FOOTBALL_APP_INPUT_GRF_INPUT_HPP

#include <optional>
#include <span>
#include <vector>

#include "app/input/grf/action.hpp"
#include "ai/default_ai.hpp"
#include "model/team.hpp"
#include "sim/player_control_set.hpp"
#include "sim/world_state.hpp"

namespace football::app::grf {

// Initial closest subset is bound in roster order, not ID/distance/phase order.
// Returned identities can be assigned to multiple frontend input slots.
std::vector<model::PlayerId> SelectInitialPlayers(
    const WorldState &world, model::TeamSide side,
    std::span<const model::PlayerId> eligible, std::size_t count);

// Application-owned input/selection state. No runtime pointer or sim callback.
// Movement/modifiers/pressure/rush are sticky. Kicks/sliding/switch are explicit
// one-shot control requests, subject to sim cadence/legality, not proof of execution.
// There is no legacy Human animation planner or implicit gauge.
class Input {
 public:
  Input(const model::Team &team, model::TeamSide side);
  bool Apply(Action action);
  bool Apply(int wire_action);
  bool IsStickyActionActive(Action action) const;
  std::optional<model::PlayerId> selected() const { return selected_; }
  bool Select(const WorldState &world, model::PlayerId player);
  void Reset();

  // Replaces only the human output. Team requests go to AI; callers then compute
  // AI controls and merge this output last before Simulation::Step.
  // Reserved IDs belong to earlier input slots, never to simulation ownership.
  void Update(const WorldState &world, ai::DefaultAI &policy,
              PlayerControlSet &output,
              std::span<const model::PlayerId> reserved = {});

 private:
  bool Eligible(const WorldPlayerState &player) const;
  model::TeamSide side_;
  std::vector<model::PlayerId> eligible_;
  std::optional<model::PlayerId> selected_;
  blunted::Vector3 direction_ = blunted::Vector3(0);
  std::optional<ControlAction> pending_action_;
  bool switch_pending_ = false;
  bool disabled_ = false;
  bool sprint_ = false;
  bool dribble_ = false;
  bool pressure_ = false;
  bool team_pressure_ = false;
  bool keeper_rush_ = false;
};

}  // namespace football::app::grf
#endif  // FOOTBALL_APP_INPUT_GRF_INPUT_HPP
