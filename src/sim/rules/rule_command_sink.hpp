#ifndef FOOTBALL_SIM_RULES_RULE_COMMAND_SINK_HPP
#define FOOTBALL_SIM_RULES_RULE_COMMAND_SINK_HPP

#include "foundation/math/vector3.hpp"
#include "sim/match/match_phase.hpp"

namespace football::sim::rules {

// Synchronous, write-only runtime consequences. No queries, retained tick inputs,
// phase-end batching or ownership transfer. Later rule checks see each mutation.
class RuleCommandSink {
 public:
  virtual ~RuleCommandSink() = default;
  virtual void StopPlay() = 0;
  virtual void StartPlay() = 0;
  virtual void StartSetPiece() = 0;
  virtual void StopSetPiece() = 0;
  virtual void StartBallInPlay() = 0;
  virtual void ResetSituation(const blunted::Vector3& position) = 0;
  virtual void ResetBall(const blunted::Vector3& position) = 0;
  virtual void SetPhase(MatchPhase phase) = 0;
};

} // namespace football::sim::rules
#endif
