#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "env/game_env.hpp"
#include "data/default_teams.hpp"
#include "model/pitch.hpp"
#include "support/diagnostics/backtrace.hpp"

namespace {
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
    steps = std::stoi(value, &consumed);
    if (consumed != value.size() || steps < 0) {
      throw std::runtime_error("--steps must be a non-negative integer");
    }
  }
  return steps;
}
}  // namespace

int main(int argc, char** argv) {
  install_stacktrace();
  std::cout.precision(17);
  std::cout << std::unitbuf;
  try {
    const int steps = ParseSteps(argc, argv);
    GameEnv environment{football::data::MakeDefaultHomeTeam(),
                        football::data::MakeDefaultAwayTeam(),
                        football::model::MakeLegacyPitch()};
    environment.start_game();
    for (int step = 0; step < steps; ++step) {
      environment.step();
    }

    const WorldState world = environment.observe();
    std::cout << "tick=" << world.tick << " players=" << world.players.size()
              << "\n";

    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << "football_app: " << error.what() << "\n";
    return EXIT_FAILURE;
  }
}
