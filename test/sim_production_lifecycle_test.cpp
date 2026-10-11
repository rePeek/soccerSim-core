#include <catch2/catch_test_macros.hpp>

#include "ai/default_ai.hpp"
#include "app/fixtures/default_teams.hpp"
#include "sim/testing/simulation_access.hpp"
#include "model/pitch.hpp"

using football::sim::testing::SimulationAccess;

namespace {
football::model::BallConfig CustomBall() {
  return football::model::BallConfig(0.45f, 0.120f, 0.5f, 0.010f);
}
}

TEST_CASE("production initialization and teardown are lifecycle-safe",
          "[sim][production-lifecycle]") {
  const auto home = football::app::fixtures::MakeDefaultHomeTeam();
  const auto away = football::app::fixtures::MakeDefaultAwayTeam();
  MatchOptions options; options.snapshot_capacity = 2;
  Simulation s;
  s.Init(home, away, football::model::Pitch{}, options);

  // The three authorities stay OFF until the explicit production entry point.
  REQUIRE_FALSE(SimulationAccess::PreparedTickProductionOf(s));
  REQUIRE_FALSE(SimulationAccess::BodyPhysicsProductionOf(s));
  REQUIRE_FALSE(SimulationAccess::BakedBodyProductionOf(s));

  SimulationAccess::InitializeProductionPhysics(s);
  REQUIRE(SimulationAccess::PreparedTickProductionOf(s));
  REQUIRE(SimulationAccess::BodyPhysicsProductionOf(s));
  REQUIRE(SimulationAccess::BakedBodyProductionOf(s));
  REQUIRE(SimulationAccess::PreparationBallOf(s) != nullptr);
  REQUIRE_FALSE(SimulationAccess::RuleContactEpisodesOf(s).empty());
  // The 15 baked identities (6 primary + 9 regions) join the coarse band in
  // the Simulation-owned owner map.
  REQUIRE(SimulationAccess::BodyColliderOwnersOf(s).size() >
          3 * SimulationAccess::TeamOf(s, 0)->GetAllPlayers().size());

  football::ai::DefaultAI policy(home, away, football::model::Pitch{});
  for (int step = 0; step < 8; ++step) {
    PlayerControlSet controls;
    policy.Update(s.Observe(), controls);
    s.Step(controls);
  }
  REQUIRE(s.Snapshots().Latest()->stamp.step_index > 0);

  REQUIRE(s.Stop());
  REQUIRE_FALSE(SimulationAccess::IsInitializedOf(s));
  REQUIRE_FALSE(SimulationAccess::PreparedTickProductionOf(s));
  REQUIRE_FALSE(SimulationAccess::BodyPhysicsProductionOf(s));
  REQUIRE_FALSE(SimulationAccess::BakedBodyProductionOf(s));
  REQUIRE(SimulationAccess::PreparationBallOf(s) == nullptr);
  REQUIRE(SimulationAccess::RuleContactEpisodesOf(s).empty());
  REQUIRE_FALSE(SimulationAccess::PendingActiveImpulseOf(s).has_value());
}

TEST_CASE("Stop then Init with different config rebuilds production caches",
          "[sim][production-lifecycle]") {
  const auto home = football::app::fixtures::MakeDefaultHomeTeam();
  const auto away = football::app::fixtures::MakeDefaultAwayTeam();
  MatchOptions options; options.snapshot_capacity = 2;
  Simulation s;
  s.Init(home, away, football::model::Pitch{}, options, football::model::BallConfig{});
  SimulationAccess::InitializeProductionPhysics(s);
  const football::ball::Ball* first =
      SimulationAccess::PreparationBallOf(s).get();
  REQUIRE(first != nullptr);
  REQUIRE(first->config().radius() == football::model::BallConfig{}.radius());
  REQUIRE(s.Stop());

  s.Init(home, away, football::model::Pitch{}, options, CustomBall());
  SimulationAccess::InitializeProductionPhysics(s);
  const football::ball::Ball* second =
      SimulationAccess::PreparationBallOf(s).get();
  REQUIRE(second != nullptr);
  // Allocator address reuse is not observable; the fresh config IS.
  REQUIRE(second->config().radius() == CustomBall().radius());
  REQUIRE(second->config().mass() == CustomBall().mass());
}

TEST_CASE("repeated Init is rejected and Stop is idempotent",
          "[sim][production-lifecycle]") {
  const auto home = football::app::fixtures::MakeDefaultHomeTeam();
  const auto away = football::app::fixtures::MakeDefaultAwayTeam();
  MatchOptions options; options.snapshot_capacity = 1;
  Simulation s;
  s.Init(home, away, football::model::Pitch{}, options);
  REQUIRE_THROWS_AS(s.Init(home, away, football::model::Pitch{}, options),
                    std::logic_error);
  REQUIRE(s.Stop());
  REQUIRE_FALSE(s.Stop());  // idempotent
  // A stopped simulation can start again with a different roster shape.
  s.Init(home, away, football::model::Pitch{}, options, CustomBall());
  REQUIRE(SimulationAccess::IsInitializedOf(s));
  REQUIRE(s.Stop());
}

TEST_CASE("reset and end change leave no stale per-tick production state",
          "[sim][production-lifecycle]") {
  const auto home = football::app::fixtures::MakeDefaultHomeTeam();
  const auto away = football::app::fixtures::MakeDefaultAwayTeam();
  MatchOptions options; options.snapshot_capacity = 2;
  Simulation s;
  s.Init(home, away, football::model::Pitch{}, options);
  SimulationAccess::InitializeProductionPhysics(s);
  football::ai::DefaultAI policy(home, away, football::model::Pitch{});
  for (int step = 0; step < 4; ++step) {
    PlayerControlSet controls;
    policy.Update(s.Observe(), controls);
    s.Step(controls);
  }
  // ResetSituation advances the generation and clears the per-tick state.
  const auto generation = SimulationAccess::ResetSequenceOf(s);
  const auto before_step = s.Snapshots().Latest()->stamp.step_index;
  s.ResetSituation(blunted::Vector3(0));
  REQUIRE(SimulationAccess::ResetSequenceOf(s) > generation);
  REQUIRE_FALSE(SimulationAccess::PendingActiveImpulseOf(s).has_value());
  REQUIRE_FALSE(SimulationAccess::CommittedBallTickOf(s).has_value());
  for (const auto& entry : SimulationAccess::RuleContactEpisodesOf(s))
    REQUIRE_FALSE(entry.second);
  PlayerControlSet controls;
  policy.Update(s.Observe(), controls);
  s.Step(controls);
  REQUIRE(s.Snapshots().Latest()->stamp.step_index > before_step);
  REQUIRE(s.Stop());
}
