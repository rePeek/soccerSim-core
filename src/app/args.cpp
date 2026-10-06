#include "app/args.hpp"

#include <charconv>
#include <stdexcept>
#include <string_view>

namespace football::app {

AppOptions ParseArgs(int argc, char** argv) {
  AppOptions options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    constexpr std::string_view prefix = "--half-duration-ms=";
    if (!argument.starts_with(prefix)) {
      throw std::runtime_error("usage: football_app [--half-duration-ms=N]");
    }
    const auto value = argument.substr(prefix.size());
    std::uint64_t parsed = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || parsed == 0) {
      throw std::runtime_error("--half-duration-ms must be a positive integer");
    }
    options.half_duration_ms = parsed;
  }
  return options;
}

}  // namespace football::app
