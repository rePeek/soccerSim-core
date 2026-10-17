#include "ai_engine.hpp"

#include <stdexcept>

#include "default_policy.hpp"
#include "eliza/eliza_policy.hpp"
#include "player/heuristic_policy.hpp"

namespace football::ai {
namespace {

std::unique_ptr<IPlayerPolicy> MakePlayerPolicy(PlayerPolicyKind kind) {
  switch (kind) {
    case PlayerPolicyKind::Default:
      return std::make_unique<DefaultPolicy>();
    case PlayerPolicyKind::Eliza:
      return std::make_unique<ElizaPolicy>();
    case PlayerPolicyKind::Heuristic:
      break;  // versioned policy, built separately
  }
  throw std::invalid_argument("unknown PlayerPolicyKind");
}

// The versioned policy that consumes transient mental effects.
std::unique_ptr<IPlayerPolicyV2> MakePlayerPolicyV2(PlayerPolicyKind kind) {
  if (kind == PlayerPolicyKind::Heuristic)
    return std::make_unique<HeuristicPolicy>();
  return nullptr;
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
      policy_(MakePlayerPolicy(config.player_policy)),
      policy_v2_(MakePlayerPolicyV2(config.player_policy)) {
  boards_[0].side = football::sim::TeamSide::Home;
  boards_[1].side = football::sim::TeamSide::Away;
}

AIEngine::~AIEngine() = default;

void AIEngine::Update(const WorldState& world, PlayerControlSet& output) {
  // Commit queued coach commands first, so this tick's policy reads the new
  // intent. A command never mutates the boards outside this single point.
  coach_.ApplyAtTick(world.tick, world);
  if (policy_v2_) {
    // Materialise the bounded transient effects once per tick, then hand the
    // policy a read-only context. Shouts stay out of WorldState and boards.
    mental_scratch_.clear();
    for (unsigned index = 0; index < 2; ++index) {
      const auto side = static_cast<football::sim::TeamSide>(index);
      for (const WorldPlayerState& player : world.players) {
        if (player.side != side) continue;
        const PlayerMentalEffect effect = coach_.MentalEffect(side, player.id);
        if (effect.motivation != 0.0f || effect.confidence != 0.0f ||
            effect.focus != 0.0f || effect.composure != 0.0f)
          mental_scratch_.push_back({side, player.id, effect});
      }
    }
    PlayerAIContext context;
    context.world = &world;
    context.tactics = boards_;
    context.mental_effects = mental_scratch_;
    policy_v2_->Update(context, output);
  } else {
    policy_->Update(world, boards_, output);
  }
}

bool AIEngine::SubmitCoachCommand(const CoachCommandEnvelope& envelope) {
  return coach_.Submit(envelope);
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
