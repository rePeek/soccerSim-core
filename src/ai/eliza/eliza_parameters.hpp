#ifndef FOOTBALL_AI_ELIZA_PARAMETERS_HPP
#define FOOTBALL_AI_ELIZA_PARAMETERS_HPP

namespace football::ai::eliza {

// AI-local capability and tuning values. They mirror the legacy GRF locomotion
// buckets but are owned by the policy; the AI never reads the internal
// PlayerMotion constants or a Player's capability profile.
struct ElizaParameters {
  float sprint_velocity = 8.0f;
  float idle_velocity = 0.0f;
  float dribble_velocity = 1.8f;
  // metres -> requested m/s gain, the legacy distanceToVelocityMultiplier.
  float distance_to_velocity = 2.6f;
  // Force-field attractor damping distance.
  float damping_distance = 7.0f;
};

inline constexpr ElizaParameters kElizaParameters{};

}  // namespace football::ai::eliza

#endif  // FOOTBALL_AI_ELIZA_PARAMETERS_HPP
