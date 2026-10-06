#include <cstdlib>
#include <exception>
#include <iostream>

#include "app/args.hpp"
#include "app/fixtures/default_teams.hpp"
#include "env/game_env.hpp"
#include "support/diagnostics/backtrace.hpp"

namespace {
const char* OutcomeName(MatchOutcome outcome) {
  switch (outcome) {
    case MatchOutcome::HomeWin: return "home_win";
    case MatchOutcome::AwayWin: return "away_win";
    case MatchOutcome::Draw: return "draw";
  }
  return "unknown";
}
}  // namespace

int main(int argc, char** argv) {
  install_stacktrace();
  try {
    const auto config = football::app::ParseArgs(argc, argv);
    MatchOptions match_options;
    if (config.half_duration_ms) match_options.half_duration_ms = *config.half_duration_ms;
    GameEnv game{football::app::fixtures::MakeDefaultHomeTeam(),
                 football::app::fixtures::MakeDefaultAwayTeam(),
                 football::model::MakeLegacyPitch(), match_options, {}};
    game.Start();
    while (!game.Finished()) game.Step();
    const MatchResult result = game.Result();
    std::cout << "home_score=" << result.home_score << " away_score=" << result.away_score
              << " outcome=" << OutcomeName(result.outcome)
              << " duration_ticks=" << result.duration_ticks << '\n';
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << "football_app: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
