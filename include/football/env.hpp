#ifndef FOOTBALL_ENV_HPP
#define FOOTBALL_ENV_HPP

#include <cstddef>
#include <memory>
#include <vector>

#include "football/ai/ai_config.hpp"
#include "football/pitch/pitch.hpp"
#include "football/ball/ball_model.hpp"
#include "football/runtime/match_config.hpp"
#include "football/runtime/simulation_config.hpp"
#include "football/runtime/result.hpp"
#include "football/observation/world_state.hpp"
#include "football/observation/snapshot.hpp"
#include "football/event/event_trajectory.hpp"
#include "football/event/match_event.hpp"

class Simulation;
namespace football::ai { class AIEngine; }

// Runs one autonomous match. Rules/results belong to sim; decisions belong to AI.
// No live control/tactics/request channel on the headless orchestration boundary.
class GameEnv {
 public:
  GameEnv(football::sim::TeamSetup home, football::sim::TeamSetup away,
          football::pitch::Pitch pitch, football::ball::BallModel ball_config,
          MatchOptions match_options,
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
  // Owning copies of the already committed sample; no resampling or disk I/O.
  // Reuses output's player allocation across calls, suitable for app recording.
  void CopyLatestSnapshot(football::sim::observation::SnapshotRecord& output) const;
  football::sim::observation::SnapshotMetadata SnapshotMetadata() const;
  // Retained oldest-to-newest window for graph consumers. False means missing
  // RAM history; never falls back to disk. Output is unchanged on false.
  bool CopySnapshotWindow(std::size_t count,
      std::vector<football::sim::observation::SnapshotRecord>& output) const;
  std::vector<football::sim::event::EventTrajectory> EventTrajectories() const;
  // Incremental, owning read of confirmed MatchEvents after `cursor`. The
  // cursor is advanced in place; a cursor from an earlier match is rebased to
  // the first event of this one. Never mutates the match.
  std::vector<football::sim::event::MatchEventRecord> ReadEvents(
      football::sim::event::MatchEventCursor& cursor) const;
  // Idempotent; releases AI and simulation. Start again uses initial declarations.
  void Stop();

 private:
  const football::sim::TeamSetup home_team_;
  const football::sim::TeamSetup away_team_;
  const football::pitch::Pitch pitch_;
  const football::ball::BallModel ball_config_;
  const MatchOptions match_options_;
  const football::ai::AIConfig ai_config_;
  std::unique_ptr<Simulation> simulation_;
  std::unique_ptr<football::ai::AIEngine> ai_;
};

#endif  // FOOTBALL_ENV_HPP
