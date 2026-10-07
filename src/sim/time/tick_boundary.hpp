#ifndef FOOTBALL_SIM_TICK_BOUNDARY_HPP
#define FOOTBALL_SIM_TICK_BOUNDARY_HPP

#include "sim/time/tick.hpp"

namespace football::sim {

// Millisecond input/output adapters, not a second simulation time domain.
// Exact conversion is intentional: rounding needs an explicit caller policy.
// During migration these also serve the remaining legacy API shims.
inline constexpr std::uint64_t kMillisecondsPerTick = 1000 / kTicksPerSecond;
static_assert(1000 % kTicksPerSecond == 0);

constexpr TickSpan TickSpanFromMillisecondsExact(std::uint64_t milliseconds) {
  if (milliseconds % kMillisecondsPerTick != 0)
    throw std::invalid_argument("time is not aligned to the simulation tick grid");
  return {milliseconds / kMillisecondsPerTick};
}

constexpr std::uint64_t ToMilliseconds(TickSpan duration) {
  return detail::MultiplyTime(duration.value, kMillisecondsPerTick);
}
constexpr std::uint64_t ToMilliseconds(Tick time) {
  return ToMilliseconds(TickSpan{time.value});
}

}  // namespace football::sim

#endif
