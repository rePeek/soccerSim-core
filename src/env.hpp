#ifndef FOOTBALL_ENV_HPP
#define FOOTBALL_ENV_HPP

#include <memory>

#include "ai/ai_config.hpp"
#include "model/pitch.hpp"
#include "model/team.hpp"
#include "sim/match_options.hpp"
#include "sim/match_result.hpp"
#include "sim/world_state.hpp"

class Simulation;
namespace football::ai { class DefaultAI; }

// Runs one autonomous match. Rules/results belong to sim; decisions belong to AI.
// No live control/tactics/request channel on the headless orchestration boundary.
class GameEnv {
 public:
  GameEnv(football::model::Team home, football::model::Team away,
          football::model::Pitch pitch, MatchOptions match_options,
          football::ai::AIConfig ai_config);
  ~GameEnv();
  GameEnv(const GameEnv&) = delete;
  GameEnv& operator=(const GameEnv&) = delete;
  GameEnv(GameEnv&&) = delete;
  GameEnv& operator=(GameEnv&&) = delete;

  // Start requires a stopped runner. Failed startup publishes no partial owners.
  void Start();
  // Exactly one simulation step; no-op after full time. Throws while stopped.
  void Step();
  // False while stopped; stopping is not a completed football match.
  bool Finished() const;
  // Final result only. Throws before full time or after Stop(). Copy before stopping.
  MatchResult Result() const;
  // Secondary, owning telemetry/replay/debug value; throws while stopped.
  WorldState Observe() const;
  // Idempotent; releases AI and simulation. Start again uses initial declarations.
  void Stop();

 private:
  const football::model::Team home_team_;
  const football::model::Team away_team_;
  const football::model::Pitch pitch_;
  const MatchOptions match_options_;
  const football::ai::AIConfig ai_config_;
  std::unique_ptr<Simulation> simulation_;
  std::unique_ptr<football::ai::DefaultAI> ai_;
};

#endif  // FOOTBALL_ENV_HPP
