// TACT-5: typed coach commands, transactional board edits and the tool registry.
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>

#include "ai/ai_engine.hpp"
#include "ai/coach/command_executor.hpp"
#include "ai/coach/tool_registry.hpp"
#include "football/ai/coach_command.hpp"

namespace {

using football::ai::BoardMode;
using football::ai::CoachCommand;
using football::ai::CoachCommandEnvelope;
using football::ai::CoachCommandExecutor;
using football::ai::CoachCommandResult;
using football::ai::CoachCommandSide;
using football::ai::CoachErrorCode;
using football::ai::CoachToolRegistry;
using football::ai::IndividualKey;
using football::ai::InstructionKey;
using football::ai::InstructionValue;
using football::ai::SetIndividualInstruction;
using football::ai::SetSlotRole;
using football::ai::SetTeamInstruction;
using football::ai::TacticalBoard;
using football::ai::TacticalSlotId;
using football::sim::TeamSide;

constexpr int kPlayers = 11;

football::sim::TeamSetup MakeTeam(int base_id) {
  football::sim::TeamSetup team;
  team.name = "coach";
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
                     const football::sim::TeamSetup& away) {
  WorldState world;
  world.tick = 500;
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

TacticalBoard MakeBoard(const football::sim::TeamSetup& team, TeamSide side) {
  return football::ai::MakeTacticalBoardV2(team, side, Pitch());
}

CoachCommandEnvelope Envelope(std::uint64_t id, std::uint64_t revision,
                              CoachCommand command, std::uint64_t observed = 500,
                              std::uint64_t expires = 100000) {
  CoachCommandEnvelope envelope;
  envelope.id = id;
  envelope.observed_tick = observed;
  envelope.expires_at_tick = expires;
  envelope.expected_board_revision = revision;
  envelope.command = std::move(command);
  return envelope;
}

}  // namespace

TEST_CASE("the tool registry lists read and write tools",
          "[ai][tactical][coach]") {
  CHECK(CoachToolRegistry::IsKnown("get_match_state"));
  CHECK(CoachToolRegistry::IsKnown("get_tactical_board"));
  CHECK(CoachToolRegistry::IsKnown("set_team_instruction"));
  CHECK(CoachToolRegistry::IsKnown("shout_to_players"));
  CHECK_FALSE(CoachToolRegistry::IsKnown("does_not_exist"));
  CHECK_FALSE(CoachToolRegistry::IsMutating("get_match_state"));
  CHECK(CoachToolRegistry::IsMutating("set_slot_anchor"));
  CHECK(CoachToolRegistry::Tools().size() == 9);
}

TEST_CASE("a team instruction commits and bumps the revision",
          "[ai][tactical][coach]") {
  const auto home = MakeTeam(0);
  const auto away = MakeTeam(100);
  WorldState world = MakeWorld(home, away);
  TacticalBoard board = MakeBoard(home, TeamSide::Home);
  const CoachCommandExecutor executor;

  SetTeamInstruction command;
  command.side = TeamSide::Home;
  command.key = InstructionKey::Tempo;
  command.value = 0.8f;
  const CoachCommandResult result =
      executor.Apply(board, Envelope(1, 0, command), world, world.tick);
  REQUIRE(result.accepted);
  CHECK(result.new_board_revision == 1);
  CHECK(board.revision == 1);
  CHECK(board.advanced->in_possession.tempo == 0.8f);
}

TEST_CASE("mentality applies the preset defaults", "[ai][tactical][coach]") {
  const auto home = MakeTeam(0);
  const auto away = MakeTeam(100);
  WorldState world = MakeWorld(home, away);
  TacticalBoard board = MakeBoard(home, TeamSide::Home);
  const CoachCommandExecutor executor;

  SetTeamInstruction command;
  command.side = TeamSide::Home;
  command.key = InstructionKey::Mentality;
  command.value = football::ai::Mentality::VeryAttacking;
  const auto result = executor.Apply(board, Envelope(1, 0, command), world, world.tick);
  REQUIRE(result.accepted);
  CHECK(board.advanced->mentality == football::ai::Mentality::VeryAttacking);
  CHECK(board.advanced->out_of_possession.pressing_intensity >
        board.advanced->out_of_possession.compactness);
}

TEST_CASE("an invalid command never changes the board",
          "[ai][tactical][coach]") {
  const auto home = MakeTeam(0);
  const auto away = MakeTeam(100);
  WorldState world = MakeWorld(home, away);
  TacticalBoard board = MakeBoard(home, TeamSide::Home);
  const CoachCommandExecutor executor;

  SetTeamInstruction command;
  command.side = TeamSide::Home;
  command.key = InstructionKey::Tempo;
  command.value = 2.0f;  // out of range
  const auto result = executor.Apply(board, Envelope(1, 0, command), world, world.tick);
  CHECK_FALSE(result.accepted);
  CHECK(result.error == CoachErrorCode::ValueOutOfRange);
  CHECK(board.revision == 0);
  CHECK(board.advanced->in_possession.tempo == 0.5f);
}

TEST_CASE("staleness, expiry and duplicate ids are rejected",
          "[ai][tactical][coach]") {
  const auto home = MakeTeam(0);
  const auto away = MakeTeam(100);
  WorldState world = MakeWorld(home, away);
  TacticalBoard board = MakeBoard(home, TeamSide::Home);
  const CoachCommandExecutor executor;

  SetTeamInstruction command;
  command.side = TeamSide::Home;
  command.key = InstructionKey::Tempo;
  command.value = 0.7f;

  SECTION("stale revision") {
    const auto result = executor.Apply(board, Envelope(1, 5, command), world, world.tick);
    CHECK(result.error == CoachErrorCode::StaleRevision);
    CHECK(board.revision == 0);
  }
  SECTION("expired tick") {
    const auto result =
        executor.Apply(board, Envelope(1, 0, command, 100, 200), world, world.tick);
    CHECK(result.error == CoachErrorCode::Expired);
  }
  SECTION("future observed tick") {
    const auto result =
        executor.Apply(board, Envelope(1, 0, command, 900), world, world.tick);
    CHECK(result.error == CoachErrorCode::FutureTick);
  }
  SECTION("legacy board") {
    TacticalBoard legacy = football::ai::MakeTacticalBoard(home, TeamSide::Home, Pitch());
    const auto result = executor.Apply(legacy, Envelope(1, 0, command), world, world.tick);
    CHECK(result.error == CoachErrorCode::LegacyBoard);
  }
  SECTION("slot out of range") {
    football::ai::SetSlotAnchor anchor;
    anchor.side = TeamSide::Home;
    anchor.slot = 11;
    anchor.phase = football::ai::TacticalPhase::InPossession;
    anchor.anchor = {0.5f, 0.0f};
    const auto result = executor.Apply(board, Envelope(1, 0, anchor), world, world.tick);
    CHECK(result.error == CoachErrorCode::InvalidSlot);
  }
  SECTION("role phase mismatch") {
    SetSlotRole role;
    role.side = TeamSide::Home;
    role.slot = 3;
    role.phase = football::ai::TacticalPhase::InPossession;
    role.role = football::ai::DefendingRole::MarkingDefender;  // wrong phase
    const auto result = executor.Apply(board, Envelope(1, 0, role), world, world.tick);
    CHECK(result.error == CoachErrorCode::MalformedCommand);
  }
}

TEST_CASE("individual marking validates opponent ownership",
          "[ai][tactical][coach]") {
  const auto home = MakeTeam(0);
  const auto away = MakeTeam(100);
  WorldState world = MakeWorld(home, away);
  TacticalBoard board = MakeBoard(home, TeamSide::Home);
  const CoachCommandExecutor executor;

  SetIndividualInstruction command;
  command.side = TeamSide::Home;
  command.slot = 2;
  command.key = IndividualKey::MarkingTarget;

  SECTION("teammate rejected") {
    command.value = home.players[5].id;
    const auto result = executor.Apply(board, Envelope(1, 0, command), world, world.tick);
    CHECK(result.error == CoachErrorCode::WrongTeam);
  }
  SECTION("opponent accepted") {
    command.value = away.players[9].id;
    const auto result = executor.Apply(board, Envelope(1, 0, command), world, world.tick);
    REQUIRE(result.accepted);
    CHECK(board.advanced->slots[2].instructions.marking_target == away.players[9].id);
  }
  SECTION("unknown player rejected") {
    command.value = static_cast<football::player::PlayerId>(4242);
    const auto result = executor.Apply(board, Envelope(1, 0, command), world, world.tick);
    CHECK(result.error == CoachErrorCode::UnknownPlayer);
  }
}

TEST_CASE("the AIEngine applies queued commands at the next tick",
          "[ai][tactical][coach]") {
  const auto home = MakeTeam(0);
  const auto away = MakeTeam(100);
  auto home_board = MakeBoard(home, TeamSide::Home);
  auto away_board = MakeBoard(away, TeamSide::Away);

  football::ai::AIConfig config;
  config.initial_tactics[0] = home_board;
  config.initial_tactics[1] = away_board;
  football::ai::AIEngine engine(home, away, Pitch(), config);
  WorldState world = MakeWorld(home, away);

  SetTeamInstruction command;
  command.side = TeamSide::Home;
  command.key = InstructionKey::Tempo;
  command.value = 0.9f;
  REQUIRE(engine.SubmitCoachCommand(Envelope(7, 0, command, 500, 100000)));

  PlayerControlSet output;
  engine.Update(world, output);
  CHECK(engine.tactics(TeamSide::Home).advanced->in_possession.tempo == 0.9f);
  CHECK(engine.tactics(TeamSide::Home).revision == 1);
  REQUIRE(engine.CoachLog().size() == 1);
  CHECK(engine.CoachLog()[0].result.accepted);
  CHECK(engine.CoachLog()[0].applied_tick == world.tick);

  // The same id cannot be replayed.
  CHECK_FALSE(engine.SubmitCoachCommand(Envelope(7, 1, command, 500, 100000)));

  // A stale command is rejected and logged without moving the board.
  CHECK(engine.SubmitCoachCommand(Envelope(8, 0, command, 500, 100000)));
  engine.Update(world, output);
  CHECK(engine.tactics(TeamSide::Home).revision == 1);
  REQUIRE(engine.CoachLog().size() == 2);
  CHECK(engine.CoachLog()[1].result.error == CoachErrorCode::StaleRevision);
  CHECK(engine.CoachLog()[1].applied_tick == 0);
}

TEST_CASE("read-only tools never expose the opponent",
          "[ai][tactical][coach]") {
  const auto home = MakeTeam(0);
  const auto away = MakeTeam(100);
  WorldState world = MakeWorld(home, away);
  auto home_board = MakeBoard(home, TeamSide::Home);

  const auto state = CoachToolRegistry::MatchState(world, TeamSide::Home);
  CHECK(state.self == TeamSide::Home);
  CHECK(state.tick == world.tick);

  const auto plan = CoachToolRegistry::TacticalPlan(home_board);
  REQUIRE(plan.has_value());
  CHECK(plan->slots.size() == 11);
  CHECK_FALSE(CoachToolRegistry::TacticalPlan(
                  football::ai::MakeTacticalBoard(home, TeamSide::Home, Pitch()))
                  .has_value());

  CHECK(CoachToolRegistry::PlayerInfo(world, TeamSide::Home, home.players[0].id)
            .has_value());
  CHECK_FALSE(CoachToolRegistry::PlayerInfo(world, TeamSide::Home, away.players[0].id)
                  .has_value());
}
