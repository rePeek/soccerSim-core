#include "app/args.hpp"

#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>

namespace football::app {

int ParseSteps(int argc, char** argv) {
  int steps = 0;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    constexpr std::string_view prefix = "--steps=";
    if (!argument.starts_with(prefix)) {
      throw std::runtime_error("usage: football_app [--steps=N]");
    }
    const std::string value(argument.substr(prefix.size()));
    std::size_t consumed = 0;
    int parsed = 0;
    try {
      parsed = std::stoi(value, &consumed);
    } catch (const std::exception&) {
      // Report empty and out-of-range counts like any other malformed value
      // instead of leaking the std::stoi exception to the caller.
      throw std::runtime_error("--steps must be a non-negative integer");
    }
    if (consumed != value.size() || parsed < 0) {
      throw std::runtime_error("--steps must be a non-negative integer");
    }
    steps = parsed;
  }
  return steps;
}

}  // namespace football::app
