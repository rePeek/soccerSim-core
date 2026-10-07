#include "sim/observation/mentalimage_sampling.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "sim/time/tick_boundary.hpp"

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

}  // namespace football::sim::observation
