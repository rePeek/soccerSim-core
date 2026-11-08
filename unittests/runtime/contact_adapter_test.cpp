// Bounded contact adapters: real actors, no match ticks or alternate physics.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <vector>
#include "football/contact/player_contact_kernel.hpp"
#include "football/contact/player_body_collider.hpp"
#include "football/ball/ball.hpp"
#include "player/player.hpp"
#include "runtime/player_contact.hpp"
#include "runtime/ball_player_contact.hpp"
#include "runtime/simulation.hpp"
#include "testing/simulation_access.hpp"

namespace {
using namespace football::sim;
using namespace football::sim::player;
using football::sim::testing::SimulationAccess;
using football::math::Vector3;

TeamSetup Team(const char* name, football::player::PlayerId first) {
  TeamSetup team;
  team.name = name;
  for (int i = 0; i < 2; ++i) {
    football::player::PlayerDefinition model;
    model.id = first + i;
    model.height = 1.75f;
    team.players.push_back(model);
  }
  team.tactical_formation = {{{-1, 0}, e_PlayerRole_GK}, {{0.4f, 0}, e_PlayerRole_CF}};
  return team;
}
void Init(Simulation& sim) {
  sim.Init(Team("home", 0), Team("away", 2), football::pitch::Pitch{},
      MatchOptions{}, football::ball::BallModel{});
}
ContactActorSnapshot Capture(const Player& actor) {
  ContactActorSnapshot out;
  out.id = actor.GetID();
  out.team_id = actor.GetTeamID();
  out.position = actor.GetPosition();
  out.collider = actor.GetGroundCollider();
  out.velocity = actor.GetMovement();
  out.facing = actor.GetDirectionVec();
  out.direction_vec = actor.GetDirectionVec();
  out.relative_body_angle = actor.GetRelBodyAngle();
  out.float_velocity = actor.GetFloatVelocity();
  out.action = actor.GetSimulationActionState();
  out.physical_balance = actor.GetStat(football::player::PlayerStat::physical_balance);
  out.decaying_position_offset = actor.GetDecayingPositionOffset();
  out.has_possession = actor.HasPossession();
  out.has_best_possession = actor.HasBestPossession();
  return out;
}
void Offset(ContactActorSnapshot& actor, const Vector3& offset) {
  actor.position += offset;
  actor.collider.center += offset;
  actor.decaying_position_offset += offset;
  if (actor.decaying_position_offset.GetLength() > 0.1f)
    actor.decaying_position_offset = actor.decaying_position_offset.GetNormalized() * 0.1f;
}

struct UnusedObservation final : PlayerObservation {
  std::size_t Size() const override { return 0; }
  Vector3 BallPrediction(std::chrono::milliseconds, int, Tick,
      const football::ball::Ball&) const override {
    FAIL("ordinary body contacts must not sample reaction history");
    return Vector3(0);
  }
  Vector3 BallPrediction(std::chrono::milliseconds, TickSpan, Tick,
      const football::ball::Ball&) const override {
    FAIL("ordinary body contacts must not sample reaction history");
    return Vector3(0);
  }
  std::vector<football::sim::player::PlayerImagePosition> TeamPlayers(int, std::chrono::milliseconds,
      Tick) const override { return {}; }
};
struct TouchRecorder final : PlayerTouchSink {
  Player& actor;
  football::player::PlayerTouchFacts& facts;
  std::vector<PlayerTouchEvent> events;
  TouchRecorder(Player& actor, football::player::PlayerTouchFacts& facts)
      : actor(actor), facts(facts) {}
  void OnAcceptedTouch(const PlayerTouchEvent& touch) override {
    events.push_back(touch);
    facts.last_team = actor.GetTeamID();
    facts.last_player_by_team[actor.GetTeamID()] = actor.GetID();
    actor.SetLastTouchTick(touch.touched_at);
  }
};
}

TEST_CASE("Runtime contact adapter resolves later pairs from corrected actors", "[runtime][contact]") {
  Simulation sim;
  Init(sim);
  const std::array<Player*, 3> actors{
      SimulationAccess::TeamOf(sim, 0).Players()[0],
      SimulationAccess::TeamOf(sim, 0).Players()[1],
      SimulationAccess::TeamOf(sim, 1).Players()[1]};
  for (std::size_t i = 0; i < actors.size(); ++i) {
    actors[i]->ResetPosition(Vector3(0.2f * i, 0, 0), Vector3(10, 0, 0));
    actors[i]->SetTeamContext(PlayerTeamFacts{});
  }
  auto* ball = SimulationAccess::BallOf(sim);
  const ContactPairEnvironment environment{ball->Predict(TickSpan{1}).position.Get2D()};
  std::array<ContactActorSnapshot, 3> expected{
      Capture(*actors[0]), Capture(*actors[1]), Capture(*actors[2])};
  std::array<std::optional<PlayerReaction>, 3> expected_trips;
  for (std::size_t i = 0; i + 1 < expected.size(); ++i) {
    for (std::size_t j = i + 1; j < expected.size(); ++j) {
      const auto result = ResolveContactPair(expected[i], expected[j], environment);
      if (result.first_has_offset) Offset(expected[i], result.first_offset);
      if (result.second_has_offset) Offset(expected[j], result.second_offset);
      if (result.first_trip.active && !expected_trips[i])
        expected_trips[i] = PlayerReaction{ReactionKind::Trip, result.first_trip.direction,
            result.first_trip.severity, actors[j]->GetID(), result.first_trip.trip_type};
      if (result.second_trip.active && !expected_trips[j])
        expected_trips[j] = PlayerReaction{ReactionKind::Trip, result.second_trip.direction,
            result.second_trip.severity, actors[i]->GetID(), result.second_trip.trip_type};
    }
  }
  std::vector<FoulAssessment> evidence;
  ResolvePlayerContacts({Tick{100}, actors, *ball, nullptr, nullptr}, evidence);
  for (std::size_t i = 0; i < actors.size(); ++i) {
    CHECK(actors[i]->GetPosition() == expected[i].position);
    CHECK(actors[i]->GetGroundCollider().center == expected[i].collider.center);
    CHECK(actors[i]->IsSpatialProjectionConsistent());
    if (expected_trips[i]) {
      REQUIRE(actors[i]->PendingReaction());
      const auto& actual = *actors[i]->PendingReaction();
      CHECK(actual.direction == expected_trips[i]->direction);
      CHECK(actual.source == expected_trips[i]->source);
      CHECK(actual.trip_type == expected_trips[i]->trip_type);
      // Reaction is staged now; animation selection waits for the next tick.
      CHECK(actors[i]->GetCurrentFunctionType() == e_FunctionType_Movement);
    }
  }
  // Attribution captures live, corrected geometry and remains frozen afterwards.
  const auto fact = AssessCollision(actors[0], actors[2], 0.9f, 0.7f, *ball, Tick{100});
  REQUIRE(fact);
  CHECK(fact->victim == actors[0]->GetID());
  CHECK(fact->victim_position == actors[0]->GetPosition());
  const auto frozen = fact->victim_position;
  actors[0]->OffsetPosition(Vector3(1, 0, 0));
  CHECK(fact->victim_position == frozen);
  CHECK_FALSE(AssessCollision(actors[0], actors[1], 1, 1, *ball, Tick{100}));
  CHECK(sim.Stop());
}

TEST_CASE("Runtime passive touch adapter preserves per-volume synchronous notifications", "[runtime][contact]") {
  Simulation sim;
  Init(sim);
  Player* home = SimulationAccess::TeamOf(sim, 0).Players()[1];
  Player* away = SimulationAccess::TeamOf(sim, 1).Players()[1];
  home->ResetPosition(Vector3(0), Vector3(10, 0, 0));
  home->SetTeamContext(PlayerTeamFacts{});
  away->ResetPosition(Vector3(5, 0, 0), Vector3(10, 0, 0));
  away->SetLastTouchTick(Tick{99});
  football::player::PlayerTouchFacts touches;
  touches.last_team = 1;
  touches.last_player_by_team[1] = away->GetID();
  std::array<Player*, 1> home_roster{home}, away_roster{away};
  std::array<PlayerTeamView, 2> teams{};
  teams[0].team_id = 0;
  teams[0].teammates = home_roster;
  teams[1].team_id = 1;
  teams[1].teammates = away_roster;
  auto* ball = SimulationAccess::BallOf(sim);
  football::ball::BallState state;
  state.position = Vector3(0.1f, 0, 1);
  state.velocity = Vector3(-5, 0, 0);
  ball->Reset(state);
  UnusedObservation observation;
  TouchRecorder recorder(*home, touches);
  const BallPlayerContactInputs inputs{teams, 1, 1, observation, Tick{100}, Tick{}, &recorder, touches};
  const auto result = ResolveBallPlayerContacts(*ball, home_roster, inputs);
  // Both torso and lower-body volumes overlap; neither may be coalesced away.
  REQUIRE(recorder.events.size() == 2);
  for (const auto& event : recorder.events) {
    CHECK(event.player == home->GetID());
    CHECK(event.type == e_TouchType_Accidental);
    CHECK(event.touched_at == Tick{100});
  }
  REQUIRE(result.impulse);
  CHECK(result.impulse->GetLength() <= state.velocity.GetLength());
  CHECK(ball->state().position == state.position);
  CHECK(ball->state().velocity == state.velocity);
  CHECK(touches.last_team == 0);
  // A subsequent sweep sees the committed touch timestamp and is suppressed.
  ResolveBallPlayerContacts(*ball, home_roster, inputs);
  CHECK(recorder.events.size() == 2);
  CHECK(sim.Stop());
}
