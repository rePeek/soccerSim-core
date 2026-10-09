#include "sim/referee/restart_readiness.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "sim/observation/pitch_frame.hpp"
#include "sim/player/player.hpp"
#include "sim/team/team.hpp"

namespace {
using blunted::Vector3;
constexpr float kTargetTolerance = 0.35f;
constexpr float kSeparationMargin = 0.5f;
// Legacy penalty-box half-width literals; keep values until the standard
// penalty-area geometry is adopted.
constexpr float kPenaltyBoxHalfWidthLegacy = 20.15f;
constexpr float kPenaltyApproachHalfWidthLegacy = 20.65f;

int Side(const Team& team) { return team.GetID() == 0 ? -1 : 1; }
Vector3 Position(const Player& player) {
  return ToHomePitchFrame(*player.GetTeam()).Position(player.GetPosition()).Get2D();
}
float OpponentDistance(e_GameMode mode) {
  if (mode == e_GameMode_ThrowIn) return 2.0f;
  if (mode == e_GameMode_KickOff || mode == e_GameMode_Corner ||
      mode == e_GameMode_FreeKick || mode == e_GameMode_Penalty) return 9.15f;
  return 0.0f;
}
bool Goalkeeper(const Player& player) {
  return player.GetTeam()->GetGoalie() == &player;
}
Vector3 LegalTarget(const RestartPlan& plan, Player& player, Vector3 target,
                    const football::model::Pitch& pitch) {
  const float half_x = pitch.half_length();
  const float half_y = pitch.half_width();
  target.coords[0] = std::clamp(target.coords[0], -half_x + 0.2f, half_x - 0.2f);
  target.coords[1] = std::clamp(target.coords[1], -half_y + 0.2f, half_y - 0.2f);
  const bool opponent = player.GetTeam() != plan.team;
  if (plan.mode == e_GameMode_KickOff)
    target.coords[0] = Side(*player.GetTeam()) * std::max(0.5f, target.coords[0] * Side(*player.GetTeam()));
  if (plan.mode == e_GameMode_Penalty && opponent && Goalkeeper(player))
    return Vector3(Side(*player.GetTeam()) * half_x, 0, 0);
  if (plan.mode == e_GameMode_Penalty) {
    const int penalty_side = plan.ball_position.coords[0] < 0 ? -1 : 1;
    target.coords[0] = penalty_side * std::min(target.coords[0] * penalty_side,
        std::min(half_x - pitch.penalty_area_depth() - kSeparationMargin, plan.ball_position.coords[0] * penalty_side - 9.65f));
  } else if (plan.mode == e_GameMode_GoalKick && opponent &&
             target.coords[0] * Side(*plan.team) > half_x - pitch.penalty_area_depth() - kSeparationMargin &&
             std::fabs(target.coords[1]) < kPenaltyApproachHalfWidthLegacy) {
    target.coords[0] = Side(*plan.team) * (half_x - pitch.penalty_area_depth() - kSeparationMargin);
  }
  const float minimum = opponent ? OpponentDistance(plan.mode) :
      plan.mode == e_GameMode_KickOff ? 1.5f : 0.0f;
  if (minimum > 0 && (target - plan.ball_position).GetLength() < minimum + kSeparationMargin) {
    const auto direction = (target - plan.ball_position).GetNormalized(
        Vector3(-Side(*plan.team), 0, 0));
    target = plan.ball_position + direction * (minimum + kSeparationMargin);
    // A sideline/corner projection must go into the pitch, not beyond it.
    target.coords[0] = std::clamp(target.coords[0], -half_x + 0.2f, half_x - 0.2f);
    target.coords[1] = std::clamp(target.coords[1], -half_y + 0.2f, half_y - 0.2f);
    if ((target - plan.ball_position).GetLength() < minimum + kSeparationMargin)
      target = plan.ball_position + (-plan.ball_position).GetNormalized(
        Vector3(Side(*player.GetTeam()), 0, 0)) * (minimum + kSeparationMargin);
  }
  target.coords[2] = 0;
  return target;
}
bool LegalPosition(const RestartPlan& plan, const Player& player, const Vector3& position,
                   const football::model::Pitch& pitch) {
  const bool opponent = player.GetTeam() != plan.team;
  if (plan.mode == e_GameMode_KickOff && position.coords[0] * Side(*player.GetTeam()) < -0.01f)
    return false;
  if (plan.mode == e_GameMode_Penalty && opponent && Goalkeeper(player))
    return std::fabs(position.coords[0] - Side(*player.GetTeam()) * pitch.half_length()) < 0.05f &&
           std::fabs(position.coords[1]) <= pitch.goal_half_width();
  if (plan.mode == e_GameMode_Penalty) {
    const int side = plan.ball_position.coords[0] < 0 ? -1 : 1;
    if (position.coords[0] * side > pitch.half_length() - pitch.penalty_area_depth() ||
        position.coords[0] * side > plan.ball_position.coords[0] * side ||
        (position - plan.ball_position).GetLength() < 9.15f) return false;
  }
  if (plan.mode == e_GameMode_GoalKick && opponent &&
      position.coords[0] * Side(*plan.team) > pitch.half_length() - pitch.penalty_area_depth() &&
      std::fabs(position.coords[1]) < kPenaltyBoxHalfWidthLegacy) return false;
  return !opponent || (position - plan.ball_position).GetLength() >= OpponentDistance(plan.mode);
}
}  // namespace

RestartPlan PlanRestart(const football::model::Pitch& pitch,
                        const Vector3& home_ball_position,
                        std::span<Player* const> active_players,
                        e_GameMode mode, Team& team) {
  RestartPlan plan;
  plan.mode = mode;
  plan.team = &team;
  plan.ball_position = home_ball_position.Get2D();
  float nearest = std::numeric_limits<float>::max();
  std::vector<Player*> active(active_players.begin(), active_players.end());
  for (auto* player : active) {
    if (player->GetTeam() != &team) continue;
    const float distance = (Position(*player) - plan.ball_position).GetSquaredLength();
    if (distance < nearest) { nearest = distance; plan.taker = player; }
  }
  std::stable_sort(active.begin(), active.end(), [&](const auto* a, const auto* b) {
    return a == plan.taker && b != plan.taker;
  });
  for (auto* player : active) {
    Vector3 target = Position(*player);
    if (player == plan.taker) {
      target = plan.ball_position + Vector3(Side(team) * 2.3f, 0, 0);
      if (mode == e_GameMode_ThrowIn)
        target = plan.ball_position + Vector3(0, plan.ball_position.coords[1] < 0 ? -0.3f : 0.3f, 0);
      if (mode == e_GameMode_KickOff || mode == e_GameMode_FreeKick)
        target = plan.ball_position + Vector3(Side(team) * 0.3f, 0, 0);
      if (mode == e_GameMode_Penalty)
        target = plan.ball_position + Vector3(Side(team) * 3.0f, 0, 0);
    } else {
      if (mode == e_GameMode_KickOff)
        target = player->GetFormationEntry().start_position *
                 Vector3(-Side(*player->GetTeam()) * pitch.half_length(),
                         -Side(*player->GetTeam()) * pitch.half_width(), 0);
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
    plan.players.push_back({player, target});
  }
  return plan;
}

bool RestartPlayersReady(const RestartPlan& plan, const football::model::Pitch& pitch) {
  if (!plan.taker || !plan.taker->IsActive()) return false;
  for (const auto& target : plan.players) {
    if (!target.player->IsActive()) continue;
    const auto position = Position(*target.player);
    if ((position - target.position).GetLength() > kTargetTolerance ||
        target.player->GetMovement().GetLength() > 0.8f) return false;
    if (target.player != plan.taker && !LegalPosition(plan, *target.player, position, pitch)) return false;
  }
  return true;
}

void PlaceRestartPlayersAtTimeout(const RestartPlan& plan) {
  for (const auto& target : plan.players) {
    if (!target.player->IsActive()) continue;
    const auto frame = FromHomePitchFrame(*target.player->GetTeam());
    target.player->ResetPosition(frame.Position(target.position), frame.Position(plan.ball_position));
  }
}
