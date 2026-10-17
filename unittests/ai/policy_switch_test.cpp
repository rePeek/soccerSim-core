// P21.7 acceptance: Default and Eliza are interchangeable through the same
// AIEngine/WorldState/PlayerControlSet, and each is deterministic.
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>

#include "ai/ai_engine.hpp"
#include "eliza_test_world.hpp"
#include "football/ai/ai_config.hpp"
#include "football/ai/default_ai.hpp"

namespace {
using football::sim::TeamSide;
using football::ai::test::MakeTeam;
using football::ai::test::MakeWorld;

PlayerControlSet Run(football::ai::PlayerPolicyKind kind, const WorldState& world,
                     const football::sim::TeamSetup& home,
                     const football::sim::TeamSetup& away) {
  football::ai::AIConfig config;
  config.player_policy = kind;
  football::ai::AIEngine engine(home, away, football::pitch::Pitch{}, config);
  PlayerControlSet output;
  engine.Update(world, output);
  return output;
}

bool Same(const PlayerControlSet& a, const PlayerControlSet& b) {
  if (a.controls().size() != b.controls().size()) return false;
  for (std::size_t i = 0; i < a.controls().size(); ++i) {
    const PlayerControl& x = a.controls()[i];
    const PlayerControl& y = b.controls()[i];
    if (x.player != y.player || x.move_direction != y.move_direction ||
        x.desired_speed != y.desired_speed || x.look_at != y.look_at ||
        x.action != y.action || x.target_player != y.target_player ||
        x.target_position != y.target_position || x.power != y.power)
      return false;
  }
  return true;
}

void CheckProtocolOnly(const PlayerControlSet& output) {
  for (const PlayerControl& control : output.controls()) {
    CHECK(control.player != football::player::kInvalidPlayerId);
    CHECK(std::isfinite(control.move_direction.coords[0]));
    CHECK(std::isfinite(control.desired_speed));
    CHECK(control.desired_speed >= 0.0f);
    CHECK(control.action <= ControlAction::Save);
  }
}
}  // namespace

TEST_CASE("Default and Eliza are interchangeable through AIEngine", "[ai]") {
  const football::sim::TeamSetup home = MakeTeam(0);
  const football::sim::TeamSetup away = MakeTeam(11);
  WorldState world = MakeWorld(home, away);
  world.teams[0].has_possession = true;
  world.teams[0].fading_possession_amount = 1.4f;
  world.ball_retainer = home.players[5].id;

  const PlayerControlSet default_output =
      Run(football::ai::PlayerPolicyKind::Default, world, home, away);
  const PlayerControlSet eliza_output =
      Run(football::ai::PlayerPolicyKind::Eliza, world, home, away);

  REQUIRE(default_output.controls().size() == 22);
  REQUIRE(eliza_output.controls().size() == 22);
  CheckProtocolOnly(default_output);
  CheckProtocolOnly(eliza_output);
  // The two implementations genuinely differ; the switch is not an alias.
  CHECK_FALSE(Same(default_output, eliza_output));
}

TEST_CASE("Each policy is deterministic for the same world", "[ai]") {
  const football::sim::TeamSetup home = MakeTeam(0);
  const football::sim::TeamSetup away = MakeTeam(11);
  const WorldState world = MakeWorld(home, away);
  for (football::ai::PlayerPolicyKind kind :
       {football::ai::PlayerPolicyKind::Default,
        football::ai::PlayerPolicyKind::Eliza}) {
    CHECK(Same(Run(kind, world, home, away), Run(kind, world, home, away)));
  }
}

TEST_CASE("AIConfig defaults to the Default policy", "[ai]") {
  const football::ai::AIConfig config;
  CHECK(config.player_policy == football::ai::PlayerPolicyKind::Default);
  CHECK_FALSE(config.initial_tactics[0].has_value());
  CHECK_FALSE(config.initial_tactics[1].has_value());
}

TEST_CASE("Default policy still matches the public DefaultAI baseline", "[ai]") {
  const football::sim::TeamSetup home = MakeTeam(0);
  const football::sim::TeamSetup away = MakeTeam(11);
  const WorldState world = MakeWorld(home, away);
  const football::ai::AIConfig config;
  football::ai::DefaultAI baseline(home, away, football::pitch::Pitch{}, config);
  PlayerControlSet expected;
  baseline.Update(world, expected);
  CHECK(Same(expected, Run(football::ai::PlayerPolicyKind::Default, world, home, away)));
}
