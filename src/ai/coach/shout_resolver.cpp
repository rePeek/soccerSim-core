#include "shout_resolver.hpp"

namespace football::ai {

PlayerMentalEffect ShoutResolver::EffectFor(ShoutType type) {
  PlayerMentalEffect effect;
  switch (type) {
    case ShoutType::Encourage:
      effect.motivation = 0.20f;
      effect.confidence = 0.10f;
      break;
    case ShoutType::Praise:
      effect.motivation = 0.05f;
      effect.confidence = 0.20f;
      effect.composure = 0.05f;
      break;
    case ShoutType::DemandMore:
      effect.motivation = 0.25f;
      effect.focus = 0.15f;
      break;
    case ShoutType::FireUp:
      effect.motivation = 0.30f;
      effect.focus = -0.10f;
      break;
    case ShoutType::Berate:
      effect.motivation = 0.15f;
      effect.composure = -0.20f;
      effect.focus = 0.10f;
      break;
    case ShoutType::Focus:
      effect.focus = 0.25f;
      effect.composure = 0.10f;
      break;
    case ShoutType::CalmDown:
      effect.composure = 0.25f;
      effect.motivation = -0.05f;
      break;
    case ShoutType::NoPressure:
      effect.composure = 0.15f;
      effect.motivation = -0.10f;
      effect.confidence = 0.05f;
      break;
  }
  return effect;
}

std::uint64_t ShoutResolver::DurationTicks(ShoutType type) {
  switch (type) {
    // 100 Hz ticks: 15 s for the intense messages, 25 s for the steadying ones.
    case ShoutType::FireUp:
    case ShoutType::Berate:
    case ShoutType::DemandMore:
      return 1500;
    case ShoutType::Encourage:
    case ShoutType::Praise:
    case ShoutType::Focus:
    case ShoutType::CalmDown:
    case ShoutType::NoPressure:
      return 2500;
  }
  return 2000;
}

}  // namespace football::ai
