#include "ai_engine.hpp"

#include <stdexcept>

#include "default_policy.hpp"
#include "eliza/eliza_policy.hpp"

namespace football::ai {
namespace {

std::unique_ptr<IPlayerPolicy> MakePlayerPolicy(PlayerPolicyKind kind) {
  switch (kind) {
    case PlayerPolicyKind::Default:
      return std::make_unique<DefaultPolicy>();
    case PlayerPolicyKind::Eliza:
      return std::make_unique<ElizaPolicy>();
  }
  throw std::invalid_argument("unknown PlayerPolicyKind");
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
      policy_(MakePlayerPolicy(config.player_policy)) {
  boards_[0].side = football::sim::TeamSide::Home;
  boards_[1].side = football::sim::TeamSide::Away;
}

AIEngine::~AIEngine() = default;

void AIEngine::Update(const WorldState& world, PlayerControlSet& output) {
  policy_->Update(world, boards_, output);
}

TacticalBoard& AIEngine::tactics(football::sim::TeamSide side) {
  return boards_.at(static_cast<unsigned>(side));
}

const TacticalBoard& AIEngine::tactics(football::sim::TeamSide side) const {
  return boards_.at(static_cast<unsigned>(side));
}

}  // namespace football::ai
