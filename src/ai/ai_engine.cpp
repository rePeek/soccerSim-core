#include "ai_engine.hpp"

#include <algorithm>

#include "coach/heuristic_coach.hpp"

namespace football::ai {
namespace {

// A command is only valid for a short window after its observation tick, so a
// delayed or replayed turn can never edit a much later match state.
constexpr std::uint64_t kCoachCommandExpiryTicks = 100;  // 1 s

// Events that justify an immediate coach turn. Fouls, offsides and restarts
// still enter the context, but they do not each force a full turn.
bool IsCoachTrigger(const football::sim::event::MatchEvent& event) {
  using namespace football::sim::event;
  return std::holds_alternative<GoalScoredEvent>(event) ||
      std::holds_alternative<CardShownEvent>(event) ||
      std::holds_alternative<PeriodEndedEvent>(event);
}
}  // namespace

AIEngine::AIEngine(const football::sim::TeamSetup& home,
                   const football::sim::TeamSetup& away,
                   const football::pitch::Pitch& pitch,
                   const AIConfig& config)
    : boards_{config.initial_tactics[0]
                  ? *config.initial_tactics[0]
                  : MakeTacticalBoard(home, football::sim::TeamSide::Home, pitch),
              config.initial_tactics[1]
                  ? *config.initial_tactics[1]
                  : MakeTacticalBoard(away, football::sim::TeamSide::Away, pitch)},
      coach_(&boards_),
      collectors_{{ContextCollector(football::sim::TeamSide::Home),
                   ContextCollector(football::sim::TeamSide::Away)}},
      schedulers_{},
      coach_policy_(config.coach_enabled
                        ? std::make_unique<HeuristicCoachPolicy>()
                        : nullptr),
      coach_enabled_(config.coach_enabled) {
  boards_[0].side = football::sim::TeamSide::Home;
  boards_[1].side = football::sim::TeamSide::Away;
}

AIEngine::~AIEngine() = default;

void AIEngine::Update(const WorldState& world, PlayerControlSet& output) {
  // Commit queued coach commands first, so the board carries this tick's
  // intent. A command never mutates the boards outside this single point.
  coach_.ApplyAtTick(world.tick, world);
  if (coach_enabled_) {
    RunCoachTurn(world);
  } else {
    pending_events_.clear();
  }
  // No player-decision layer exists yet. Emit no control so Player and
  // Simulation keep their deterministic neutral idle execution; a failed or
  // absent intent never falls back to a heuristic.
  output.Clear();
}

bool AIEngine::SubmitCoachCommand(const CoachCommandEnvelope& envelope) {
  return coach_.Submit(envelope);
}

void AIEngine::ConsumeMatchEvents(
    std::span<const football::sim::event::MatchEventRecord> events) {
  pending_events_.assign(events.begin(), events.end());
}

int AIEngine::CoachCalls(football::sim::TeamSide side) const {
  return schedulers_[static_cast<unsigned>(side)].CallsUsed();
}

void AIEngine::RunCoachTurn(const WorldState& world) {
  const bool event_triggered =
      std::any_of(pending_events_.begin(), pending_events_.end(),
                  [](const auto& record) { return IsCoachTrigger(record.event); });
  for (unsigned index = 0; index < 2; ++index) {
    const auto side = static_cast<football::sim::TeamSide>(index);
    // Fold this observation and the confirmed events once per tick.
    collectors_[index].OnTick(world, coach_.ActiveShouts(side), pending_events_);
    const CoachContext context = collectors_[index].Build(world, boards_[index]);
    if (!schedulers_[index].ShouldRun(context, event_triggered)) continue;
    std::vector<CoachCommand> commands;
    coach_policy_->RunTurn(context, commands);
    for (CoachCommand& command : commands) {
      CoachCommandEnvelope envelope;
      envelope.id = next_command_id_++;
      envelope.observed_tick = world.tick;
      envelope.expires_at_tick = world.tick + kCoachCommandExpiryTicks;
      envelope.expected_board_revision = boards_[index].revision;
      envelope.command = std::move(command);
      coach_.Submit(envelope);
    }
    schedulers_[index].OnTurnRun(world.tick, event_triggered);
  }
  pending_events_.clear();
}

const std::vector<CoachCommandRecord>& AIEngine::CoachLog() const {
  return coach_.log();
}

void AIEngine::SetShoutCooldownTicks(std::uint64_t ticks) {
  coach_.SetShoutCooldownTicks(ticks);
}

const std::vector<ActiveShout>& AIEngine::ActiveShouts(
    football::sim::TeamSide side) const {
  return coach_.ActiveShouts(side);
}

PlayerMentalEffect AIEngine::PlayerMentalEffectOf(
    football::sim::TeamSide side, football::player::PlayerId player) const {
  return coach_.MentalEffect(side, player);
}

TacticalBoard& AIEngine::tactics(football::sim::TeamSide side) {
  return boards_.at(static_cast<unsigned>(side));
}

const TacticalBoard& AIEngine::tactics(football::sim::TeamSide side) const {
  return boards_.at(static_cast<unsigned>(side));
}

}  // namespace football::ai
