#ifndef FOOTBALL_SIM_TICK_HPP
#define FOOTBALL_SIM_TICK_HPP

#include <compare>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace football::sim {

// One simulation tick is the discrete time quantum: 1/100 s physical time.
// New simulation timestamps, deadlines, durations and cadences use Tick or
// TickSpan. Millisecond conversion belongs at API/config boundaries; migration
// adapters must not silently quantize legacy sub-tick estimates or clocks.
inline constexpr std::uint64_t kTicksPerSecond = 100;
inline constexpr float kTickSeconds = 1.0f / static_cast<float>(kTicksPerSecond);

struct TickSpan {
  std::uint64_t value = 0;
  auto operator<=>(const TickSpan&) const = default;
};

struct Tick {
  std::uint64_t value = 0;
  auto operator<=>(const Tick&) const = default;
};

namespace detail {
constexpr std::uint64_t AddTime(std::uint64_t a, std::uint64_t b) {
  if (b > std::numeric_limits<std::uint64_t>::max() - a)
    throw std::overflow_error("tick addition overflow");
  return a + b;
}
constexpr std::uint64_t SubtractTime(std::uint64_t a, std::uint64_t b) {
  if (b > a) throw std::invalid_argument("negative tick interval");
  return a - b;
}
constexpr std::uint64_t MultiplyTime(std::uint64_t value, std::uint64_t factor) {
  if (value > std::numeric_limits<std::uint64_t>::max() / factor)
    throw std::overflow_error("tick duration overflow");
  return value * factor;
}
}  // namespace detail

constexpr Tick operator+(Tick time, TickSpan duration) {
  return {detail::AddTime(time.value, duration.value)};
}
constexpr Tick operator-(Tick time, TickSpan duration) {
  return {detail::SubtractTime(time.value, duration.value)};
}
constexpr TickSpan operator-(Tick later, Tick earlier) {
  return {detail::SubtractTime(later.value, earlier.value)};
}
constexpr TickSpan operator+(TickSpan a, TickSpan b) {
  return {detail::AddTime(a.value, b.value)};
}
constexpr TickSpan operator-(TickSpan a, TickSpan b) {
  return {detail::SubtractTime(a.value, b.value)};
}
constexpr Tick& operator+=(Tick& time, TickSpan duration) {
  return time = time + duration;
}
constexpr TickSpan& operator+=(TickSpan& a, TickSpan b) {
  return a = a + b;
}

constexpr TickSpan Seconds(std::uint64_t seconds) {
  return {detail::MultiplyTime(seconds, kTicksPerSecond)};
}
constexpr TickSpan Minutes(std::uint64_t minutes) {
  return Seconds(detail::MultiplyTime(minutes, 60));
}
constexpr float ToSeconds(TickSpan duration) {
  return static_cast<float>(duration.value) * kTickSeconds;
}

}  // namespace football::sim

#endif
