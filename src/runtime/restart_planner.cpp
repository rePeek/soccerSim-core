#include "runtime/restart_planner.hpp"
#include "football/pitch/pitch.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

#include "football/pitch/pitch_frame.hpp"
#include "player/player.hpp"
#include "football/team/selection.hpp"


namespace football::sim {
namespace {
using football::math::Vector3;
constexpr float kTargetTolerance = 0.35f;
constexpr float kSeparationMargin = 0.5f;
// Legacy penalty-box half-width literals; keep values until the standard
// penalty-area geometry is adopted.
constexpr float kPenaltyBoxHalfWidthLegacy = 20.15f;
constexpr float kPenaltyApproachHalfWidthLegacy = 20.65f;

int Side(int team_id) { return team_id == 0 ? -1 : 1; }
int Side(const Player& player) { return player.GetTeamID() == 0 ? -1 : 1; }
Vector3 Position(const Player& player) {
  return ToHomePitchFrame(player.GetTeamSide() == football::sim::TeamSide::Home, player.GetDynamicSide()).Position(player.GetPosition()).Get2D();
}
float OpponentDistance(e_GameMode mode) {
  if (mode == e_GameMode_ThrowIn) return 2.0f;
  if (mode == e_GameMode_KickOff || mode == e_GameMode_Corner ||
      mode == e_GameMode_FreeKick || mode == e_GameMode_Penalty) return 9.15f;
  return 0.0f;
}
bool Goalkeeper(const Player& player) {
  return player.GetFormationEntry().role == e_PlayerRole_GK;
}
Vector3 LegalTarget(const RestartPlan& plan, Player& player, Vector3 target,
                    const football::pitch::Pitch& pitch) {
  const float half_x = pitch.half_length();
  const float half_y = pitch.half_width();
  target.coords[0] = std::clamp(target.coords[0], -half_x + 0.2f, half_x - 0.2f);
  target.coords[1] = std::clamp(target.coords[1], -half_y + 0.2f, half_y - 0.2f);
  const bool opponent = player.GetTeamID() != plan.team_id;
  if (plan.mode == e_GameMode_KickOff)
    target.coords[0] = Side(player) * std::max(0.5f, target.coords[0] * Side(player));
  if (plan.mode == e_GameMode_Penalty && opponent && Goalkeeper(player))
    return Vector3(Side(player) * half_x, 0, 0);
  if (plan.mode == e_GameMode_Penalty) {
    const int penalty_side = plan.ball_position.coords[0] < 0 ? -1 : 1;
    target.coords[0] = penalty_side * std::min(target.coords[0] * penalty_side,
        std::min(half_x - pitch.penalty_area_depth() - kSeparationMargin, plan.ball_position.coords[0] * penalty_side - 9.65f));
  } else if (plan.mode == e_GameMode_GoalKick && opponent &&
             target.coords[0] * Side(plan.team_id) > half_x - pitch.penalty_area_depth() - kSeparationMargin &&
             std::fabs(target.coords[1]) < kPenaltyApproachHalfWidthLegacy) {
    target.coords[0] = Side(plan.team_id) * (half_x - pitch.penalty_area_depth() - kSeparationMargin);
  }
  const float minimum = opponent ? OpponentDistance(plan.mode) :
      plan.mode == e_GameMode_KickOff ? 1.5f : 0.0f;
  if (minimum > 0 && (target - plan.ball_position).GetLength() < minimum + kSeparationMargin) {
    const auto direction = (target - plan.ball_position).GetNormalized(
        Vector3(-Side(plan.team_id), 0, 0));
    target = plan.ball_position + direction * (minimum + kSeparationMargin);
    // A sideline/corner projection must go into the pitch, not beyond it.
    target.coords[0] = std::clamp(target.coords[0], -half_x + 0.2f, half_x - 0.2f);
    target.coords[1] = std::clamp(target.coords[1], -half_y + 0.2f, half_y - 0.2f);
    if ((target - plan.ball_position).GetLength() < minimum + kSeparationMargin)
      target = plan.ball_position + (-plan.ball_position).GetNormalized(
        Vector3(Side(player), 0, 0)) * (minimum + kSeparationMargin);
  }
  target.coords[2] = 0;
  return target;
}
bool LegalPosition(const RestartPlan& plan, const Player& player, const Vector3& position,
                   const football::pitch::Pitch& pitch) {
  const bool opponent = player.GetTeamID() != plan.team_id;
  if (plan.mode == e_GameMode_KickOff && position.coords[0] * Side(player) < -0.01f)
    return false;
  if (plan.mode == e_GameMode_Penalty && opponent && Goalkeeper(player))
    return std::fabs(position.coords[0] - Side(player) * pitch.half_length()) < 0.05f &&
           std::fabs(position.coords[1]) <= pitch.goal_half_width();
  if (plan.mode == e_GameMode_Penalty) {
    const int side = plan.ball_position.coords[0] < 0 ? -1 : 1;
    if (position.coords[0] * side > pitch.half_length() - pitch.penalty_area_depth() ||
        position.coords[0] * side > plan.ball_position.coords[0] * side ||
        (position - plan.ball_position).GetLength() < 9.15f) return false;
  }
  if (plan.mode == e_GameMode_GoalKick && opponent &&
      position.coords[0] * Side(plan.team_id) > pitch.half_length() - pitch.penalty_area_depth() &&
      std::fabs(position.coords[1]) < kPenaltyBoxHalfWidthLegacy) return false;
  return !opponent || (position - plan.ball_position).GetLength() >= OpponentDistance(plan.mode);
}
// Team-driven restart planning helpers. All are pure: they read static Team
// configuration plus explicit live candidates and never mutate actors or RNG.
int AttackingDirection(int team_id) { return -Side(team_id); }

std::optional<football::team::FormationPresetKind> PresetKindFor(e_GameMode mode) {
  using football::team::FormationPresetKind;
  switch (mode) {
    case e_GameMode_KickOff: return FormationPresetKind::KickOff;
    case e_GameMode_FreeKick: return FormationPresetKind::FreeKick;
    case e_GameMode_Corner: return FormationPresetKind::Corner;
    case e_GameMode_Penalty: return FormationPresetKind::Penalty;
    case e_GameMode_ThrowIn: return FormationPresetKind::ThrowIn;
    default: return std::nullopt;
  }
}

// Declared taker priority for one restart. Free kicks expose the direct list as
// primary and the indirect list as secondary: e_GameMode cannot currently
// distinguish the two rules, so this is only a taker-selection compatibility
// fallback, not a statement about the restart's rule category.
struct TakerOrders {
  const std::vector<football::player::PlayerId>* primary = nullptr;
  const std::vector<football::player::PlayerId>* secondary = nullptr;
};

TakerOrders TakerPriority(e_GameMode mode, int team_id, const Vector3& ball,
                          const football::team::SetPieceOrders* orders) {
  if (orders == nullptr) return {};
  switch (mode) {
    case e_GameMode_Penalty: return {&orders->penalties, nullptr};
    case e_GameMode_FreeKick:
      return {&orders->direct_free_kicks, &orders->indirect_free_kicks};
    case e_GameMode_Corner: {
      // Left/right is the attacking team's own perspective in the home frame.
      const bool left = ball.coords[1] * AttackingDirection(team_id) > 0;
      return {left ? &orders->corners_left : &orders->corners_right, nullptr};
    }
    case e_GameMode_ThrowIn: return {&orders->throw_ins, nullptr};
    default: return {};
  }
}

const football::team::FormationPreset* FindPreset(
    const football::team::FormationPresets* presets, e_GameMode mode,
    football::team::FormationPresetSide side) {
  if (presets == nullptr) return nullptr;
  const std::optional<football::team::FormationPresetKind> kind = PresetKindFor(mode);
  if (!kind) return nullptr;
  for (const football::team::FormationPreset& preset : presets->presets) {
    if (preset.kind == *kind && preset.side == side) return &preset;
  }
  return nullptr;
}

std::optional<Vector3> PresetTargetFor(
    const football::team::FormationPreset* preset,
    const football::sim::ActiveLineupState* lineup, const Player& player,
    const football::pitch::Pitch& pitch, const Vector3& home_ball) {
  if (preset == nullptr || lineup == nullptr) return std::nullopt;
  for (const football::team::PresetPosition& position : preset->positions) {
    if (position.slot >= lineup->slots.size()) continue;
    const std::optional<football::player::PlayerId>& bound = lineup->slots[position.slot];
    if (!bound || *bound != player.GetID()) continue;
    // Home-frame candidate; LegalTarget below owns the rule legalization.
    return football::team::FormationAnchorToWorld(
        position.anchor, pitch, Side(player.GetTeamID()), home_ball, false);
  }
  return std::nullopt;
}

Player* SelectTaker(std::span<Player* const> active, e_GameMode mode, int team_id,
                    const Vector3& ball, const RestartPlanningTable& planning) {
  std::vector<Player*> candidates;
  for (Player* player : active) {
    if (player->GetTeamID() == team_id) candidates.push_back(player);
  }
  // Nearest-first, then stable roster order: exactly the legacy ranking.
  std::stable_sort(candidates.begin(), candidates.end(),
                   [&](const Player* a, const Player* b) {
                     return (Position(*a) - ball).GetSquaredLength() <
                         (Position(*b) - ball).GetSquaredLength();
                   });
  const TakerOrders priority = TakerPriority(
      mode, team_id, ball, planning.For(team_id).orders);
  std::vector<football::team::EligiblePlayer> eligible;
  eligible.reserve(candidates.size());
  for (const Player* player : candidates) {
    eligible.push_back({player->GetID(), true});
  }
  const auto declared = [&](const std::vector<football::player::PlayerId>* list)
      -> std::optional<football::player::PlayerId> {
    if (list == nullptr) return std::nullopt;
    return football::team::SelectEligibleTaker(
        *list, eligible, football::team::SelectionFallback::None);
  };
  std::optional<football::player::PlayerId> selected = declared(priority.primary);
  if (!selected) selected = declared(priority.secondary);
  if (!selected) {
    // No declared (or ineligible) order: the legacy nearest-first ranking.
    selected = football::team::SelectEligibleTaker(
        {}, eligible, football::team::SelectionFallback::CandidateOrder);
  }
  if (selected) {
    for (Player* player : candidates) {
      if (player->GetID() == *selected) return player;
    }
  }
  // No declared or fallback candidate: the readiness/replan path handles it.
  return candidates.empty() ? nullptr : candidates.front();
}
}  // namespace

RestartPlan PlanRestart(const football::pitch::Pitch& pitch,
                        const Vector3& home_ball_position,
                        std::span<Player* const> active_players,
                        e_GameMode mode, int team_id,
                        const RestartPlanningTable& planning) {
  RestartPlan plan;
  plan.mode = mode;
  plan.team_id = team_id;
  plan.ball_position = home_ball_position.Get2D();

  std::vector<Player*> active(active_players.begin(), active_players.end());
  Player* taker = SelectTaker(active, mode, team_id, plan.ball_position, planning);
  plan.taker = taker != nullptr ? taker->GetID() : football::player::kInvalidPlayerId;
  std::stable_sort(active.begin(), active.end(), [&](const auto* a, const auto* b) {
    return a->GetID() == plan.taker && b->GetID() != plan.taker;
  });

  using football::team::FormationPresetSide;
  const football::team::FormationPreset* attacking_preset =
      FindPreset(planning.For(team_id).presets, mode, FormationPresetSide::Attacking);
  const football::team::FormationPreset* defending_preset =
      FindPreset(planning.For(1 - team_id).presets, mode, FormationPresetSide::Defending);

  for (auto* player : active) {
    Vector3 target = Position(*player);
    if (player->GetID() == plan.taker) {
      target = plan.ball_position + Vector3(Side(team_id) * 2.3f, 0, 0);
      if (mode == e_GameMode_ThrowIn)
        target = plan.ball_position + Vector3(0, plan.ball_position.coords[1] < 0 ? -0.3f : 0.3f, 0);
      if (mode == e_GameMode_KickOff || mode == e_GameMode_FreeKick)
        target = plan.ball_position + Vector3(Side(team_id) * 0.3f, 0, 0);
      if (mode == e_GameMode_Penalty)
        target = plan.ball_position + Vector3(Side(team_id) * 3.0f, 0, 0);
    } else {
      const bool opponent = player->GetTeamID() != plan.team_id;
      const football::team::FormationPreset* preset =
          opponent ? defending_preset : attacking_preset;
      const football::sim::ActiveLineupState* lineup =
          opponent ? planning.For(1 - team_id).lineup : planning.For(team_id).lineup;
      const std::optional<Vector3> preset_target =
          PresetTargetFor(preset, lineup, *player, pitch, plan.ball_position);
      if (preset_target) {
        target = *preset_target;
      } else if (mode == e_GameMode_KickOff) {
        target = player->GetFormationEntry().start_position *
                 Vector3(-Side(*player) * pitch.half_length(),
                         -Side(*player) * pitch.half_width(), 0);
      }
      target = LegalTarget(plan, *player, target, pitch);
      const auto available = [&](const Vector3& candidate) {
        return LegalPosition(plan, *player, candidate, pitch) &&
            std::none_of(plan.players.begin(), plan.players.end(), [&](const auto& other) {
              return (candidate - other.position).GetLength() < 1.5f;
            });
      };
      // Clipping at a line/wall must not assign multiple actors one point.
      // Search a deterministic local grid, reserving the taker's position first.
      const auto origin = target;
      for (int ring = 1; ring <= 16 && !available(target); ++ring) {
        bool found = false;
        for (int x = -ring; x <= ring && !found; ++x) {
          for (int y = -ring; y <= ring; ++y) {
            if (std::abs(x) != ring && std::abs(y) != ring) continue;
            const auto candidate = LegalTarget(plan, *player, origin + Vector3(x * 1.5f, y * 1.5f, 0), pitch);
            if (available(candidate)) { target = candidate; found = true; break; }
          }
        }
      }
      if (!available(target)) throw std::logic_error("restart cannot reserve legal actor positions");
    }
    plan.players.push_back({player->GetID(), target});
  }
  return plan;
}

Player* RestartRoster::Find(football::player::PlayerId id) const {
  for (Player* player : home) {
    if (player->GetID() == id) return player;
  }
  for (Player* player : away) {
    if (player->GetID() == id) return player;
  }
  return nullptr;
}

bool RestartPlayersReady(const RestartPlan& plan, const football::pitch::Pitch& pitch,
                         const RestartRoster& roster) {
  Player* taker = roster.Find(plan.taker);
  if (taker == nullptr || !taker->IsActive()) return false;
  for (const auto& target : plan.players) {
    Player* player = roster.Find(target.player);
    if (player == nullptr || !player->IsActive()) continue;
    const auto position = Position(*player);
    if ((position - target.position).GetLength() > kTargetTolerance ||
        player->GetMovement().GetLength() > 0.8f) return false;
    if (player != taker && !LegalPosition(plan, *player, position, pitch)) return false;
  }
  return true;
}

void PlaceRestartPlayersAtTimeout(const RestartPlan& plan, const RestartRoster& roster) {
  for (const auto& target : plan.players) {
    Player* player = roster.Find(target.player);
    if (player == nullptr || !player->IsActive()) continue;
    const auto frame = FromHomePitchFrame(player->GetTeamSide() == football::sim::TeamSide::Home, player->GetDynamicSide());
    player->ResetPosition(frame.Position(target.position), frame.Position(plan.ball_position));
  }
}

}  // namespace football::sim
