#include <cstdlib>
#include <iostream>
#include <string>

#include "env/game_env.hpp"


namespace {

void PrintPosition(const char* label, const Position& position) {
  std::cout << label << "=(" << position.env_coord(0) << ", "
            << position.env_coord(1) << ", " << position.env_coord(2) << ')';
}

void PrintState(int iteration, const SharedInfo& state) {
  std::cout << "iteration=" << iteration << " step=" << state.step << ' ';
  PrintPosition("ball", state.ball_position);
  std::cout << " score=" << state.left_goals << ':' << state.right_goals;

  if (!state.left_team.empty() && !state.right_team.empty()) {
    std::cout << ' ';
    PrintPosition("left_player_0", state.left_team.front().player_position);
    std::cout << ' ';
    PrintPosition("right_player_0", state.right_team.front().player_position);
  }

  std::cout << " in_play=" << state.is_in_play << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  const int steps = argc > 1 ? std::atoi(argv[1]) : 1000;
  if (steps < 1) {
    std::cerr << "Usage: " << argv[0] << " [positive-step-count]\n";
    return 2;
  }

  if (!std::getenv("GFOOTBALL_DATA_DIR")) {
    std::cerr << "Set GFOOTBALL_DATA_DIR before running " << argv[0] << ".\n";
    return 2;
  }

  GameEnv env;
  env.game_config.render = false;
  env.start_game();

  auto config = ScenarioConfig::make();
  config->left_agents = 0;
  config->right_agents = 0;
  config->real_time = false;
  config->deterministic = true;
  config->game_engine_random_seed = 42;

  // Empty formations deliberately use TeamData's built-in 11-player formation.
  env.reset(*config, false);

  for (int i = 0; i < steps; ++i) {
    env.step();
    if (i == 0 || (i + 1) % 100 == 0 || i + 1 == steps) {
      PrintState(i + 1, env.get_info());
    }
  }

  // GameEnv currently has no owner-managed shutdown API. Process teardown is
  // intentional here; a later runtime API can make shutdown testable.
  return 0;
}
