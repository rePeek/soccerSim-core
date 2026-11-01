#ifndef FOOTBALL_APP_ARGS_HPP
#define FOOTBALL_APP_ARGS_HPP

#include <cstdint>
#include <optional>

namespace football::app {

// Application parsing only. Unspecified duration uses sim's regulation default.
struct AppOptions {
  std::optional<std::uint64_t> half_duration_ms;
  // Capacity of the in-memory snapshot history, not a disk recording option.
  std::optional<std::uint64_t> snapshot_capacity;
};
AppOptions ParseArgs(int argc, char** argv);

}  // namespace football::app

#endif  // FOOTBALL_APP_ARGS_HPP
