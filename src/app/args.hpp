#ifndef FOOTBALL_APP_ARGS_HPP
#define FOOTBALL_APP_ARGS_HPP

namespace football::app {

// Parses the CLI arguments of football_app. `--steps=N` sets the number of
// simulation ticks to run and defaults to 0 when the flag is absent; a later
// occurrence overrides an earlier one. Throws std::runtime_error on anything
// that is not a non-negative `--steps=` integer.
int ParseSteps(int argc, char** argv);

}  // namespace football::app

#endif  // FOOTBALL_APP_ARGS_HPP
