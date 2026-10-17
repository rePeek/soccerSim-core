#include "context_collector.hpp"

#include <algorithm>
#include <cmath>

namespace football::ai {
namespace {
using football::sim::TeamSide;

float AttackSign(const WorldState& world, TeamSide side) {
  return -static_cast<float>(world.teams[static_cast<unsigned>(side)].defending_direction);
}
}  // namespace

void ContextCollector::Reset() {
  shouts_.clear();
  samples_.clear();
  events_.clear();
  last_sample_tick_ = 0;
  have_sample_ = false;
  last_self_score_ = 0;
  last_opponent_score_ = 0;
  last_phase_ = MatchPhase::PreMatch;
  last_restart_ = e_GameMode_Normal;
  last_in_play_ = false;
  have_event_state_ = false;
}

void ContextCollector::FoldSample(const WorldState& world) {
  if (have_sample_ && world.tick - last_sample_tick_ < kSampleIntervalTicks) return;
  Sample sample;
  sample.tick = world.tick;
  sample.possession = world.teams[static_cast<unsigned>(self_)].has_possession;
  sample.ball_in_play = world.ball_in_play;
  const float attack = AttackSign(world, self_);
  sample.ball_x_fraction =
      0.5f + world.ball_position.coords[0] * attack / world.pitch.length();
  samples_.push_back(sample);
  last_sample_tick_ = world.tick;
  have_sample_ = true;
  // Bound the ring: drop samples older than the 15-minute window, then cap.
  const std::uint64_t cutoff = world.tick > kWindowTicks ? world.tick - kWindowTicks : 0;
  while (!samples_.empty() && samples_.front().tick < cutoff) samples_.pop_front();
  while (samples_.size() > kMaxSamples) samples_.pop_front();
}

void ContextCollector::DetectEvents(const WorldState& world) {
  const auto opponent = self_ == TeamSide::Home ? TeamSide::Away : TeamSide::Home;
  const int self_score = world.teams[static_cast<unsigned>(self_)].score;
  const int opponent_score = world.teams[static_cast<unsigned>(opponent)].score;

  auto push = [&](CoachEventType type, TeamSide side, e_GameMode restart) {
    MatchEventSummary event;
    event.type = type;
    event.side = side;
    event.tick = world.tick;
    event.restart = restart;
    events_.push_back(event);
    if (events_.size() > kMaxEvents) events_.erase(events_.begin());
  };

  if (!have_event_state_) {
    push(CoachEventType::KickOff, self_, e_GameMode_KickOff);
    have_event_state_ = true;
  } else {
    if (self_score > last_self_score_) push(CoachEventType::Goal, self_, world.restart);
    if (opponent_score > last_opponent_score_)
      push(CoachEventType::Goal, opponent, world.restart);
    if (world.phase != last_phase_) push(CoachEventType::PeriodChange, self_, world.restart);
    if (world.restart != last_restart_ && world.restart != e_GameMode_Normal)
      push(CoachEventType::Restart, self_, world.restart);
    if (world.in_play != last_in_play_)
      push(world.in_play ? CoachEventType::BallInPlay : CoachEventType::BallOutOfPlay,
           self_, world.restart);
  }
  last_self_score_ = self_score;
  last_opponent_score_ = opponent_score;
  last_phase_ = world.phase;
  last_restart_ = world.restart;
  last_in_play_ = world.in_play;
}

void ContextCollector::OnTick(const WorldState& world,
                              const std::vector<ActiveShout>& shouts) {
  shouts_ = shouts;
  FoldSample(world);
  DetectEvents(world);
}

TeamTrendSummary ContextCollector::Summarise(std::uint64_t window) const {
  TeamTrendSummary summary;
  if (samples_.empty()) return summary;
  const std::uint64_t latest = samples_.back().tick;
  const std::uint64_t cutoff = latest > window ? latest - window : 0;
  float possession = 0.0f;
  float ball_x = 0.0f;
  float in_play = 0.0f;
  int count = 0;
  for (const Sample& sample : samples_) {
    if (sample.tick < cutoff) continue;
    possession += sample.possession ? 1.0f : 0.0f;
    ball_x += sample.ball_x_fraction;
    in_play += sample.ball_in_play ? 1.0f : 0.0f;
    ++count;
  }
  if (count == 0) return summary;
  summary.sample_count = count;
  summary.possession_fraction = possession / static_cast<float>(count);
  summary.mean_ball_x_fraction = ball_x / static_cast<float>(count);
  summary.ball_in_play_fraction = in_play / static_cast<float>(count);
  return summary;
}

CoachContext ContextCollector::Build(const WorldState& world,
                                     const TacticalBoard& own_board) const {
  const auto opponent = self_ == TeamSide::Home ? TeamSide::Away : TeamSide::Home;
  CoachContext context;
  context.self = self_;
  context.observed_tick = world.tick;
  context.board_revision = own_board.revision;
  context.phase = world.phase;
  context.self_score = world.teams[static_cast<unsigned>(self_)].score;
  context.opponent_score = world.teams[static_cast<unsigned>(opponent)].score;
  context.clock.regulation_ticks = world.regulation_time.value;
  context.clock.half_underway = world.half_underway;
  context.clock.ball_in_play = world.ball_in_play;
  context.last_5_min = Summarise(5 * 60 * football::sim::kTicksPerSecond);
  context.last_15_min = Summarise(kWindowTicks);
  context.recent_events = events_;
  context.active_shouts = shouts_;
  if (const TacticalPlanV2* plan = AdvancedPlan(own_board)) context.current_tactics = *plan;

  for (const WorldPlayerState& player : world.players) {
    if (player.side != self_) continue;
    PlayerSummary summary;
    summary.id = player.id;
    summary.role = player.role;
    summary.position = player.position;
    summary.active = player.active;
    summary.has_possession = player.has_possession;
    summary.fatigue_inv = player.fatigue_inv;
    context.own_players.push_back(summary);
  }
  return context;
}

}  // namespace football::ai
