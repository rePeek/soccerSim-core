#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

#include "app/args.hpp"
#include "app/fixtures/default_teams.hpp"
#include "env/game_env.hpp"
#include "model/pitch.hpp"
#include "support/diagnostics/backtrace.hpp"

int main(int argc, char** argv) {
  install_stacktrace();
  std::cout.precision(17);
  std::cout << std::unitbuf;
  try {
    const int steps = football::app::ParseSteps(argc, argv);
    GameEnv environment{football::app::fixtures::MakeDefaultHomeTeam(),
                        football::app::fixtures::MakeDefaultAwayTeam(),
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
