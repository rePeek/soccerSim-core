// P4d-1.2 value-only diagnostics. Never used to accept a gameplay touch.
#ifndef FOOTBALL_PLAYER_BODY_CONTACT_DIAGNOSTICS_HPP
#define FOOTBALL_PLAYER_BODY_CONTACT_DIAGNOSTICS_HPP
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <vector>
#include "sim/player/player_action.hpp"
#include "football/ball/collider.hpp"

inline constexpr std::size_t kBodyActionCount = e_FunctionType_Special + 1;
inline constexpr const char* kBodyActionNames[kBodyActionCount] = {
    "none", "movement", "ball_control", "trap", "short_pass", "long_pass",
    "high_pass", "header", "shot", "deflect", "catch", "interfere", "trip", "sliding", "special"};

// Boundary = scheduled frame lies in [elapsed, elapsed+1]. Past is not an
// indefinitely active contact window; Pending is reported separately.
enum class BodyTouchWindow { Unscheduled, Pending, Boundary, Past };
inline BodyTouchWindow BodyActionTouchWindow(const PlayerActionState& action) {
  if (!action.contact) return BodyTouchWindow::Unscheduled;
  if (action.elapsed > *action.contact) return BodyTouchWindow::Past;
  if (action.contact->value - action.elapsed.value <= 1) return BodyTouchWindow::Boundary;
  return BodyTouchWindow::Pending;
}
inline bool BodyFootAction(e_FunctionType action) {
  return action == e_FunctionType_Shot || action == e_FunctionType_ShortPass ||
      action == e_FunctionType_LongPass || action == e_FunctionType_HighPass ||
      action == e_FunctionType_Trap || action == e_FunctionType_BallControl;
}

// Pre-sweep facts, NOT a replay of the live legacy solver's mutable touch biases.
// Bits overlap; controlled routing/unavailable history are not proven rejection.
enum BodyLegacyBlock : std::uint32_t {
  BodyCooldown = 1u << 0, BodyOwnRecentTouch = 1u << 1,
  BodyNoOpponentTouch = 1u << 2, BodyActionExcluded = 1u << 3,
  BodyUniquePossession = 1u << 4, BodyUnexpectedDirection = 1u << 5,
  BodyHistoryUnavailable = 1u << 6, BodyPlayGate = 1u << 7,
  BodyOldGeometryMiss = 1u << 8, BodyControlledRoute = 1u << 9,
};
inline constexpr std::size_t kBodyLegacyBlockCount = 10;
inline constexpr const char* kBodyLegacyBlockNames[kBodyLegacyBlockCount] = {
    "cooldown", "own_recent_touch", "no_opponent_touch", "action_excluded",
    "unique_possession", "direction_gate", "history_unavailable", "play_gate",
    "old_geometry_miss", "controlled_route"};

struct BodyImpactClassification {
  e_FunctionType action = e_FunctionType_None;
  BodyTouchWindow window = BodyTouchWindow::Unscheduled;
  std::uint32_t legacy_blocks = 0;
  bool initial_penetration = false;
  bool position_corrected = false;
  bool starts_separated = false;
  bool foot_conflict = false;
  bool continued_geometry = false;
  bool continued_impact = false;
  std::uint64_t geometry_length = 0;
  std::size_t candidate_parts = 0, candidate_players = 0;
  float ball_speed = 0, player_speed = 0, closing_speed = 0;
  std::uint64_t intentional_touches = 0; // real accepted action evidence in this interval
};
struct BodyImpactGroup {
  std::uint64_t impacts = 0, unmatched = 0, first = 0, continued = 0;
  std::uint64_t penetration = 0, corrected = 0, separated = 0;
  std::uint64_t boundary = 0, pending = 0, foot_conflicts = 0;
  std::uint64_t multi_parts = 0, multi_players = 0;
  std::array<std::uint64_t, kBodyLegacyBlockCount> blocks{};
  double ball_speed_sum = 0, player_speed_sum = 0, closing_speed_sum = 0;
  std::uint64_t intervals_with_active_touch = 0;
  void Record(const BodyImpactClassification& c, bool matched) {
    ++impacts;
    if (!matched) ++unmatched;
    if (c.continued_geometry) ++continued; else ++first;
    penetration += c.initial_penetration;
    corrected += c.position_corrected;
    separated += c.starts_separated;
    boundary += c.window == BodyTouchWindow::Boundary;
    pending += c.window == BodyTouchWindow::Pending;
    foot_conflicts += c.foot_conflict;
    multi_parts += c.candidate_parts > 1;
    multi_players += c.candidate_players > 1;
    for (std::size_t i = 0; i < blocks.size(); ++i) blocks[i] += (c.legacy_blocks >> i) & 1u;
    ball_speed_sum += c.ball_speed;
    player_speed_sum += c.player_speed;
    closing_speed_sum += c.closing_speed;
    intervals_with_active_touch += c.intentional_touches != 0;
  }
};

// Fixed collider slots. An episode is a consecutive observed-contact run, not
// an AcceptedTouch. Known separation at the next boundary starts a NEW run even
// on consecutive ticks. Gaps/generation changes cannot bridge an episode.
struct BodyEpisodeObservation { bool continued = false; std::uint64_t length = 1; };
class BodyContactEpisodes {
 public:
  void Resize(std::size_t slots) { tracks_.assign(slots, {}); }
  void Break() { std::fill(tracks_.begin(), tracks_.end(), Track{}); }
  BodyEpisodeObservation Observe(std::size_t slot, std::uint64_t step,
                                 std::uint64_t generation, bool starts_separated) {
    auto& t = tracks_.at(slot);
    const bool continued = t.active && t.generation == generation &&
        t.step != std::numeric_limits<std::uint64_t>::max() && t.step + 1 == step && !starts_separated;
    t.length = continued ? t.length + 1 : 1;
    t.active = true;
    t.step = step;
    t.generation = generation;
    return {continued, t.length};
  }
  void Complete(std::uint64_t step, std::uint64_t generation) {
    for (auto& t : tracks_) if (t.step != step || t.generation != generation) t.active = false;
  }
 private:
  struct Track { std::uint64_t step = 0, generation = 0, length = 0; bool active = false; };
  std::vector<Track> tracks_;
};
#endif
