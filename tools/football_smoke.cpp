#include <cstdlib>
#include <iostream>

#include "app/fixtures/default_teams.hpp"
#include "env/game_env.hpp"
#include "support/diagnostics/backtrace.hpp"

int main(int argc, char** argv) {
  install_stacktrace();
  std::cout.precision(17);
  std::cout << std::unitbuf;
  const int ticks = argc > 1 ? std::atoi(argv[1]) : 1000;
  if (ticks < 1) {
    std::cerr << "Usage: " << argv[0] << " [positive-tick-count]\n";
    return 2;
  }

  GameEnv env{football::app::fixtures::MakeDefaultHomeTeam(),
              football::app::fixtures::MakeDefaultAwayTeam(),
              football::model::MakeLegacyPitch()};
  env.start_game();
  for (int tick = 0; tick < ticks; ++tick) {
    env.step();
    if (tick == 0 || (tick + 1) % 100 == 0 || tick + 1 == ticks) {
      const WorldState world = env.observe();
      std::cout << "tick=" << world.tick << " players=" << world.players.size()
                << " ball=(" << world.ball_position.coords[0] << ", "
                << world.ball_position.coords[1] << ", "
                << world.ball_position.coords[2] << ")\n";
    }
  }
  env.stop_game();
  return 0;
}
