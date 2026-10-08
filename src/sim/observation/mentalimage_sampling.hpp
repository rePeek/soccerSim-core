#ifndef FOOTBALL_SIM_OBSERVATION_MENTALIMAGE_SAMPLING_HPP
#define FOOTBALL_SIM_OBSERVATION_MENTALIMAGE_SAMPLING_HPP

#include <chrono>
#include <cstddef>
#include <span>

#include "sim/time/tick.hpp"

namespace football::ball { class Ball; }
class MentalImage;

namespace football::sim::observation {

// Execution-history capture/sampling policy, not the simulation time quantum.
inline constexpr TickSpan kMentalImageCadence{10};

// Half-up nearest capture slot, clamped to history size. Empty history throws.
std::size_t MentalImageSampleIndex(std::size_t size, TickSpan age);
std::size_t MentalImageSampleIndex(std::size_t size,
                                  std::chrono::milliseconds age);

MentalImage* SampleMentalImage(std::span<MentalImage> images, TickSpan age);
MentalImage* SampleMentalImage(std::span<MentalImage> images,
                              std::chrono::milliseconds age);
// Synchronous touch feedback: only the newest capture's ball predictions refresh.
void RefreshLatestMentalImageBallPredictions(std::span<MentalImage> images,
                                            const football::ball::Ball& ball);

}  // namespace football::sim::observation

#endif  // FOOTBALL_SIM_OBSERVATION_MENTALIMAGE_SAMPLING_HPP
