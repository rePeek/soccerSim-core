#ifndef FOOTBALL_TEST_REFEREE_PROCESS_FIXTURE_HPP
#define FOOTBALL_TEST_REFEREE_PROCESS_FIXTURE_HPP

#include "sim/referee/referee.hpp"
#include "sim/referee/referee_view.hpp"

namespace football::test {

// Test-only orchestration of the split referee stages in the legacy order. The
// production path lives in Simulation::ProcessReferee; this keeps direct referee
// unit tests able to exercise one instant without a full Simulation.
inline void ProcessReferee(Referee& referee,
                           const football::sim::rules::RefereeTickFacts& facts,
                           const MatchOptions& options, blunted::Rng& rng,
                           football::sim::rules::RuleCommandSink& commands) {
  if (facts.phase == MatchPhase::Finished) return;
  const football::sim::rules::RefereeView view{facts};
  const bool was_restart =
      referee.GetBuffer().active && referee.GetBuffer().restart.has_value();
  referee.Advance(view, options, rng, commands);
  if (was_restart) return;
  if (facts.play_authorized && !facts.set_piece_active) {
    referee.EvaluateOutOfPlay(view, commands);
    referee.CheckPendingFoul(view, commands);
  }
}

}  // namespace football::test

#endif  // FOOTBALL_TEST_REFEREE_PROCESS_FIXTURE_HPP
