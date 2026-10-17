// TACT-6a: bounded, transient coach shouts. They never touch the persistent
// board, respect cooldown, stack within a cap and expire on a fixed tick.
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>

#include "ai/coach/coach_runtime.hpp"
#include "football/ai/coach_command.hpp"

namespace {

using football::ai::ActiveShout;
using football::ai::CoachCommand;
using football::ai::CoachCommandEnvelope;
using football::ai::CoachErrorCode;
using football::ai::CoachRuntime;
using football::ai::IssueShout;
using football::ai::PlayerMentalEffect;
using football::ai::ShoutType;
using football::ai::TacticalBoard;
using football::sim::TeamSide;

constexpr int kPlayers = 11;

football::sim::TeamSetup MakeTeam(int base_id) {
  football::sim::TeamSetup team;
  team.name = "shout";
  constexpr e_PlayerRole kRoles[kPlayers] = {
      e_PlayerRole_GK, e_PlayerRole_LB, e_PlayerRole_CB, e_PlayerRole_RB,
      e_PlayerRole_DM, e_PlayerRole_CM, e_PlayerRole_LM, e_PlayerRole_RM,
      e_PlayerRole_AM, e_PlayerRole_CF, e_PlayerRole_CF};
  for (int i = 0; i < kPlayers; ++i) {
    football::player::PlayerDefinition definition;
    definition.id = static_cast<football::player::PlayerId>(base_id + i);
    definition.first_name = "p";
    definition.last_name = std::to_string(i);
    definition.height = 1.8f;
    team.players.push_back(definition);
    football::sim::TacticalFormationEntry entry;
    entry.position.x = -0.85f + 0.17f * static_cast<float>(i);
    entry.position.y = (i % 2 == 0 ? -0.3f : 0.3f);
    entry.role = kRoles[i];
    team.tactical_formation.push_back(entry);
  }
  return team;
}

const football::pitch::Pitch& Pitch() {
  static const football::pitch::Pitch pitch;
  return pitch;
}

WorldState MakeWorld(const football::sim::TeamSetup& home,
                     const football::sim::TeamSetup& away,
                     std::uint64_t tick = 1000) {
  WorldState world;
  world.tick = tick;
  world.phase = MatchPhase::FirstHalf;
  world.half_underway = true;
  world.in_play = true;
  world.ball_in_play = true;
  world.ball_position = football::math::Vector3(0.f, 0.f, 0.11f);
  world.pitch = Pitch();
  world.teams = {{{TeamSide::Home, -1, 0}, {TeamSide::Away, 1, 0}}};
  auto add = [&](const football::sim::TeamSetup& team, TeamSide side, float sign) {
    for (int i = 0; i < kPlayers; ++i) {
      WorldPlayerState player;
      player.id = team.players[i].id;
      player.side = side;
      player.position = football::math::Vector3(sign * (2.f + 4.f * i), 0.f, 0.f);
      player.facing = football::math::Vector3(sign, 0.f, 0.f);
      player.active = true;
      player.max_speed = 8.f;
      player.role = team.tactical_formation[i].role;
      world.players.push_back(player);
    }
  };
  add(home, TeamSide::Home, -1.f);
  add(away, TeamSide::Away, 1.f);
  return world;
}

CoachCommandEnvelope ShoutEnvelope(std::uint64_t id, TeamSide side, ShoutType type,
                                   std::vector<football::player::PlayerId> targets,
                                   std::uint64_t revision = 0) {
  IssueShout shout;
  shout.side = side;
  shout.type = type;
  shout.targets = std::move(targets);
  CoachCommandEnvelope envelope;
  envelope.id = id;
  envelope.observed_tick = 0;
  envelope.expires_at_tick = 0;
  envelope.expected_board_revision = revision;
  envelope.command = shout;
  return envelope;
}

struct Fixture {
  football::sim::TeamSetup home = MakeTeam(0);
  football::sim::TeamSetup away = MakeTeam(100);
  WorldState world = MakeWorld(home, away);
  std::array<TacticalBoard, 2> boards = {
      football::ai::MakeTacticalBoardV2(home, TeamSide::Home, Pitch()),
      football::ai::MakeTacticalBoardV2(away, TeamSide::Away, Pitch())};
  CoachRuntime runtime{&boards};
};

}  // namespace

TEST_CASE("a shout is transient and never edits the board",
          "[ai][tactical][shout]") {
  Fixture fixture;
  const float tempo_before = fixture.boards[0].advanced->in_possession.tempo;
  const std::uint64_t revision_before = fixture.boards[0].revision;

  REQUIRE(fixture.runtime.Submit(ShoutEnvelope(
      1, TeamSide::Home, ShoutType::Encourage, {fixture.home.players[9].id})));
  fixture.runtime.ApplyAtTick(fixture.world.tick, fixture.world);

  REQUIRE(fixture.runtime.ActiveShouts(TeamSide::Home).size() == 1);
  const ActiveShout& active = fixture.runtime.ActiveShouts(TeamSide::Home)[0];
  CHECK(active.target == fixture.home.players[9].id);
  CHECK(active.type == ShoutType::Encourage);
  CHECK(active.issued_tick == fixture.world.tick);
  CHECK(active.expires_at_tick == fixture.world.tick + 2500);

  CHECK(fixture.boards[0].revision == revision_before);
  CHECK(fixture.boards[0].advanced->in_possession.tempo == tempo_before);
  REQUIRE(fixture.runtime.log().size() == 1);
  CHECK(fixture.runtime.log()[0].result.accepted);
  CHECK(fixture.runtime.log()[0].before_revision ==
        fixture.runtime.log()[0].after_revision);
}

TEST_CASE("cooldown limits repeated shouts", "[ai][tactical][shout]") {
  Fixture fixture;
  fixture.runtime.SetShoutCooldownTicks(100);

  REQUIRE(fixture.runtime.Submit(ShoutEnvelope(
      1, TeamSide::Home, ShoutType::DemandMore, {fixture.home.players[9].id})));
  fixture.runtime.ApplyAtTick(1000, fixture.world);
  CHECK(fixture.runtime.ActiveShouts(TeamSide::Home).size() == 1);

  // Too soon: rejected, board and shouts unchanged.
  REQUIRE(fixture.runtime.Submit(ShoutEnvelope(
      2, TeamSide::Home, ShoutType::Praise, {fixture.home.players[9].id})));
  fixture.runtime.ApplyAtTick(1050, fixture.world);
  REQUIRE(fixture.runtime.log().size() == 2);
  CHECK(fixture.runtime.log()[1].result.error == CoachErrorCode::ShoutCooldown);
  CHECK(fixture.runtime.ActiveShouts(TeamSide::Home).size() == 1);

  // After the cooldown it is accepted.
  REQUIRE(fixture.runtime.Submit(ShoutEnvelope(
      3, TeamSide::Home, ShoutType::Praise, {fixture.home.players[9].id})));
  fixture.runtime.ApplyAtTick(1200, fixture.world);
  CHECK(fixture.runtime.log()[2].result.accepted);
  CHECK(fixture.runtime.ActiveShouts(TeamSide::Home).size() == 2);
}

TEST_CASE("shouts expire on a fixed tick", "[ai][tactical][shout]") {
  Fixture fixture;
  REQUIRE(fixture.runtime.Submit(ShoutEnvelope(
      1, TeamSide::Home, ShoutType::FireUp, {fixture.home.players[9].id})));
  fixture.runtime.ApplyAtTick(1000, fixture.world);
  CHECK(fixture.runtime.ActiveShouts(TeamSide::Home).size() == 1);

  const PlayerMentalEffect effect_before =
      fixture.runtime.MentalEffect(TeamSide::Home, fixture.home.players[9].id);
  CHECK(effect_before.motivation > 0.0f);

  // FireUp lasts 1500 ticks; step past expiry and prune.
  WorldState later = fixture.world;
  later.tick = 1000 + 1500;
  fixture.runtime.ApplyAtTick(later.tick, later);
  CHECK(fixture.runtime.ActiveShouts(TeamSide::Home).empty());
  const PlayerMentalEffect effect_after =
      fixture.runtime.MentalEffect(TeamSide::Home, fixture.home.players[9].id);
  CHECK(effect_after.motivation == 0.0f);
}

TEST_CASE("same-type shouts refresh and different types cap",
          "[ai][tactical][shout]") {
  Fixture fixture;
  fixture.runtime.SetShoutCooldownTicks(0);
  const auto target = fixture.home.players[9].id;

  REQUIRE(fixture.runtime.Submit(
      ShoutEnvelope(1, TeamSide::Home, ShoutType::Focus, {target})));
  fixture.runtime.ApplyAtTick(1000, fixture.world);
  REQUIRE(fixture.runtime.Submit(
      ShoutEnvelope(2, TeamSide::Home, ShoutType::Focus, {target})));
  fixture.runtime.ApplyAtTick(1010, fixture.world);
  CHECK(fixture.runtime.ActiveShouts(TeamSide::Home).size() == 1);  // refreshed

  REQUIRE(fixture.runtime.Submit(
      ShoutEnvelope(3, TeamSide::Home, ShoutType::Encourage, {target})));
  fixture.runtime.ApplyAtTick(1020, fixture.world);
  REQUIRE(fixture.runtime.Submit(
      ShoutEnvelope(4, TeamSide::Home, ShoutType::CalmDown, {target})));
  fixture.runtime.ApplyAtTick(1030, fixture.world);
  CHECK(fixture.runtime.ActiveShouts(TeamSide::Home).size() ==
        CoachRuntime::kMaxActiveShoutsPerPlayer);
}

TEST_CASE("illegal shout targets are rejected without side effects",
          "[ai][tactical][shout]") {
  Fixture fixture;
  auto apply = [&](std::uint64_t id, TeamSide side, ShoutType type,
                   std::vector<football::player::PlayerId> targets) {
    fixture.runtime.Submit(ShoutEnvelope(id, side, type, std::move(targets)));
    fixture.runtime.ApplyAtTick(1000, fixture.world);
    return fixture.runtime.log().back().result;
  };

  CHECK(apply(1, TeamSide::Home, ShoutType::Encourage, {})
            .error == CoachErrorCode::NoShoutTargets);
  CHECK(apply(2, TeamSide::Home, ShoutType::Encourage,
              {static_cast<football::player::PlayerId>(9999)})
            .error == CoachErrorCode::UnknownPlayer);
  CHECK(apply(3, TeamSide::Home, ShoutType::Encourage,
              {fixture.away.players[9].id})
            .error == CoachErrorCode::WrongTeam);
  CHECK(fixture.runtime.ActiveShouts(TeamSide::Home).empty());
  CHECK(fixture.boards[0].revision == 0);
}
