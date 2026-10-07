#ifndef FOOTBALL_SIM_OBSERVATION_MENTALIMAGE_SAMPLING_HPP
#define FOOTBALL_SIM_OBSERVATION_MENTALIMAGE_SAMPLING_HPP

#include <chrono>
#include <cstddef>

#include "sim/time/tick.hpp"

namespace football::sim::observation {

// Execution-history capture/sampling policy, not the simulation time quantum.
inline constexpr TickSpan kMentalImageCadence{10};

// Half-up nearest capture slot, clamped to history size. Empty history throws.
std::size_t MentalImageSampleIndex(std::size_t size, TickSpan age);
std::size_t MentalImageSampleIndex(std::size_t size,
                                  std::chrono::milliseconds age);

}  // namespace football::sim::observation

#endif  // FOOTBALL_SIM_OBSERVATION_MENTALIMAGE_SAMPLING_HPP
