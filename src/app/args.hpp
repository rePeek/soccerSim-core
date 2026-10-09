#ifndef FOOTBALL_APP_ARGS_HPP
#define FOOTBALL_APP_ARGS_HPP

#include <cstdint>
#include <filesystem>
#include <optional>

namespace football::app {

// Application parsing only. Unspecified duration uses sim's regulation default.
struct AppOptions {
  std::optional<std::uint64_t> half_duration_ms;
  std::optional<std::uint64_t> snapshot_capacity;
  std::optional<std::filesystem::path> snapshot_path;
};
AppOptions ParseArgs(int argc, char** argv);

}  // namespace football::app

#endif  // FOOTBALL_APP_ARGS_HPP
