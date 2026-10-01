#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "control/control_system.hpp"

namespace {
class TestWorld final : public WorldStateView {
 public:
  std::uint64_t tick() const override { return 17; }
  blunted::Vector3 ball_position() const override { return blunted::Vector3(0); }
  std::span<const WorldPlayerState> players() const override { return {}; }
};

class TestCoach final : public CoachControl {
 public:
  TestCoach(std::vector<std::string>& trace, float width)
      : trace_(trace), width_(width) {}

  TeamPlan Decide(const CoachControlContext& context) override {
    trace_.push_back("coach" + std::to_string(context.self));
    return TeamPlan{.width = width_};
  }

  void Reset() override { ++resets; }
  int resets = 0;

 private:
  std::vector<std::string>& trace_;
  float width_;
};

class TestPlayer final : public PlayerControl {
 public:
  explicit TestPlayer(std::vector<std::string>& trace) : trace_(trace) {}

  PlayerIntent Decide(const PlayerControlContext& context) override {
    trace_.push_back("player" + std::to_string(context.self));
    PlayerIntent intent;
    intent.desired_speed = context.team_plan.width;
    return intent;
  }

  void Reset() override { ++resets; }
  int resets = 0;

 private:
  std::vector<std::string>& trace_;
};

void Require(bool condition) {
  if (!condition) std::exit(EXIT_FAILURE);
}
}  // namespace

int main() {
  std::vector<std::string> trace;
  ControlSystem controls;
  controls.SetCoach(0, std::make_unique<TestCoach>(trace, 1.0f));
  controls.SetCoach(1, std::make_unique<TestCoach>(trace, 2.0f));
  controls.SetPlayerControl(1, 17, std::make_unique<TestPlayer>(trace));
  controls.SetPlayerControl(0, 3, std::make_unique<TestPlayer>(trace));

  TestWorld world;
  controls.Step(world);

  Require((trace == std::vector<std::string>{"coach0", "coach1", "player17",
                                             "player3"}));
  Require(controls.team_plan(0).team == 0 && controls.team_plan(1).team == 1);
  Require(controls.player_intents().size() == 2);
  Require(controls.player_intents()[0].player == 17);
  Require(controls.player_intents()[0].intent.desired_speed == 2.0f);
  Require(controls.player_intents()[1].player == 3);
  Require(controls.player_intents()[1].intent.desired_speed == 1.0f);

  controls.Reset();
  Require(controls.player_intents().empty());
  return EXIT_SUCCESS;
}
