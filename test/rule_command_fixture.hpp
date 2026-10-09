#ifndef FOOTBALL_TEST_RULE_COMMAND_FIXTURE_HPP
#define FOOTBALL_TEST_RULE_COMMAND_FIXTURE_HPP

#include <functional>
#include <string>
#include <vector>
#include "sim/referee/rule_command_sink.hpp"

namespace football::test {
// Test-only command recorder/decorator; no runtime query interface.
struct RuleCommandProbe final : football::sim::rules::RuleCommandSink {
  football::sim::rules::RuleCommandSink* delegate = nullptr;
  std::function<void(const blunted::Vector3&)> reset;
  std::vector<std::string> calls;
  explicit RuleCommandProbe(football::sim::rules::RuleCommandSink* delegate = nullptr)
      : delegate(delegate) {}
  void StopPlay() override { calls.emplace_back("stop"); if (delegate) delegate->StopPlay(); }
  void StartPlay() override { calls.emplace_back("start"); if (delegate) delegate->StartPlay(); }
  void StartSetPiece() override { calls.emplace_back("setpiece"); if (delegate) delegate->StartSetPiece(); }
  void StopSetPiece() override { calls.emplace_back("end-setpiece"); if (delegate) delegate->StopSetPiece(); }
  void StartBallInPlay() override { calls.emplace_back("ball-live"); if (delegate) delegate->StartBallInPlay(); }
  void SetBallRetainer(Player* retainer) override {
    calls.emplace_back("retain"); if (delegate) delegate->SetBallRetainer(retainer);
  }
  void ResetSituation(const blunted::Vector3& position) override {
    calls.emplace_back("reset");
    if (reset) reset(position); else if (delegate) delegate->ResetSituation(position);
  }
  void ResetBall(const blunted::Vector3& position) override {
    calls.emplace_back("reset-ball"); if (delegate) delegate->ResetBall(position);
  }
  void SetPhase(MatchPhase phase) override { calls.emplace_back("phase"); if (delegate) delegate->SetPhase(phase); }
};
} // namespace football::test
#endif
