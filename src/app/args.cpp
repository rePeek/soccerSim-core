#include "app/args.hpp"

#include <charconv>
#include <stdexcept>
#include <string_view>
#include <limits>
#include <string>

namespace football::app {

AppOptions ParseArgs(int argc, char** argv) {
  AppOptions options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument.starts_with("--snapshot=")) {
      const auto value = argument.substr(std::string_view("--snapshot=").size());
      if (value.empty()) throw std::runtime_error("--snapshot requires an output path");
      options.snapshot_path = std::filesystem::path(value);
      continue;
    }
    const bool duration = argument.starts_with("--half-duration-ms=");
    const bool capacity = argument.starts_with("--snapshot-capacity=");
    if (!duration && !capacity) {
      throw std::runtime_error("usage: football_app [--half-duration-ms=N] "
                               "[--snapshot=PATH] [--snapshot-capacity=N]");
    }
    const auto prefix = duration ? std::string_view("--half-duration-ms=")
                                 : std::string_view("--snapshot-capacity=");
    const auto value = argument.substr(prefix.size());
    std::uint64_t parsed = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || parsed == 0 ||
        (capacity && parsed > std::numeric_limits<std::size_t>::max())) {
      throw std::runtime_error(std::string(prefix.substr(0, prefix.size() - 1)) +
                               " must be a positive integer");
    }
    if (duration) options.half_duration_ms = parsed;
    else options.snapshot_capacity = parsed;
  }
  return options;
}

}  // namespace football::app
