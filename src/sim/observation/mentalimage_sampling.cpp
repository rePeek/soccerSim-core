#include "sim/observation/mentalimage_sampling.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "sim/time/tick_boundary.hpp"
#include "sim/ball/ball.hpp"
#include "sim/observation/mentalimage.hpp"

namespace football::sim::observation {

std::size_t MentalImageSampleIndex(std::size_t size, TickSpan age) {
  if (size == 0) throw std::logic_error("mental-image history is empty");
  const auto cadence = kMentalImageCadence.value;
  auto index = std::min<std::uint64_t>(age.value / cadence, size - 1);
  if (age.value % cadence >= cadence / 2 && index < size - 1) ++index;
  return index;
}

std::size_t MentalImageSampleIndex(std::size_t size,
                                  std::chrono::milliseconds age) {
  if (size == 0) throw std::logic_error("mental-image history is empty");
  // Preserve the legacy float -> double ratio, half-up rounding and clamp before
  // narrowing. Continuous reaction delays are not tick-grid deadlines.
  const double capture_ms = static_cast<double>(ToMilliseconds(kMentalImageCadence));
  const double slot = std::round(static_cast<float>(age.count()) / capture_ms);
  return static_cast<std::size_t>(std::clamp(slot, 0.0, static_cast<double>(size - 1)));
}

MentalImage* SampleMentalImage(std::span<MentalImage> images, TickSpan age) {
  return &images[MentalImageSampleIndex(images.size(), age)];
}

MentalImage* SampleMentalImage(std::span<MentalImage> images,
                              std::chrono::milliseconds age) {
  return &images[MentalImageSampleIndex(images.size(), age)];
}

void RefreshLatestMentalImageBallPredictions(std::span<MentalImage> images,
                                            const Ball& ball) {
  if (!images.empty()) images.front().UpdateBallPredictions(ball);
}

}  // namespace football::sim::observation
