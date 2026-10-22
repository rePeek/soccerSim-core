#include "context_collector.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <variant>

namespace football::ai {
namespace {
using football::sim::TeamSide;

float AttackSign(const WorldState& world, TeamSide side) {
  return -static_cast<float>(world.teams[static_cast<unsigned>(side)].defending_direction);
}

// Formal MatchEvent -> coach vocabulary. Only confirmed, coach-relevant events
// map; passes and shots stay out of the coach's event list.
std::optional<CoachEventType> CoachTypeOf(
    const football::sim::event::MatchEvent& event) {
  using namespace football::sim::event;
  if (std::holds_alternative<GoalScoredEvent>(event)) return CoachEventType::Goal;
  if (std::holds_alternative<CardShownEvent>(event)) return CoachEventType::Card;
  if (std::holds_alternative<FoulConfirmedEvent>(event)) return CoachEventType::Foul;
  if (std::holds_alternative<OffsideConfirmedEvent>(event)) return CoachEventType::Offside;
  if (std::holds_alternative<RestartAwardedEvent>(event)) return CoachEventType::Restart;
  if (std::holds_alternative<RestartTakenEvent>(event)) return CoachEventType::BallInPlay;
  if (std::holds_alternative<PeriodEndedEvent>(event)) return CoachEventType::PeriodChange;
  return std::nullopt;
}

TeamSide EventSide(const football::sim::event::MatchEvent& event, TeamSide fallback) {
  using namespace football::sim::event;
  if (const auto* e = std::get_if<GoalScoredEvent>(&event)) return e->team;
  if (const auto* e = std::get_if<CardShownEvent>(&event)) return e->team;
  if (const auto* e = std::get_if<FoulConfirmedEvent>(&event)) return e->team;
  if (const auto* e = std::get_if<OffsideConfirmedEvent>(&event)) return e->team;
  if (const auto* e = std::get_if<RestartAwardedEvent>(&event)) return e->team;
  if (const auto* e = std::get_if<RestartTakenEvent>(&event)) return e->team;
  return fallback;
}

e_GameMode EventRestart(const football::sim::event::MatchEvent& event) {
  using namespace football::sim::event;
  if (const auto* e = std::get_if<RestartAwardedEvent>(&event)) return e->kind;
  if (const auto* e = std::get_if<RestartTakenEvent>(&event)) return e->kind;
  return e_GameMode_Normal;
}

std::uint64_t EventTick(const football::sim::event::MatchEvent& event) {
  return std::visit([](const auto& value) { return value.tick.value; }, event);
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
  last_event_sequence_ = 0;
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

void ContextCollector::IngestEvents(
    std::span<const football::sim::event::MatchEventRecord> events) {
  for (const football::sim::event::MatchEventRecord& record : events) {
    if (record.sequence <= last_event_sequence_) continue;  // idempotent replay
    last_event_sequence_ = record.sequence;
    const std::optional<CoachEventType> type = CoachTypeOf(record.event);
    if (!type) continue;
    MatchEventSummary summary;
    summary.type = *type;
    summary.side = EventSide(record.event, self_);
    summary.tick = EventTick(record.event);
    summary.restart = EventRestart(record.event);
    events_.push_back(summary);
    if (events_.size() > kMaxEvents) events_.erase(events_.begin());
  }
}

void ContextCollector::OnTick(
    const WorldState& world, const std::vector<ActiveShout>& shouts,
    std::span<const football::sim::event::MatchEventRecord> events) {
  shouts_ = shouts;
  FoldSample(world);
  if (!events.empty()) {
    // Runtime confirmed these events; do not re-derive them from WorldState.
    IngestEvents(events);
  } else {
    DetectEvents(world);
  }
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
  context.current_tactics = own_board.plan;

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
