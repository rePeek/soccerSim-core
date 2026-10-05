#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "control/tactical_board.hpp"
#include "data/default_teams.hpp"
#include "data/player_profile.hpp"
#include "sim/match.hpp"
#include "sim/player/player.hpp"
#include "sim/simulation.hpp"

namespace model = football::model;
static_assert(std::is_same_v<model::PlayerId, std::uint32_t>);
static_assert(std::is_same_v<PlayerIndex, std::uint8_t>);
static_assert(!std::is_same_v<PlayerIndex, model::PlayerId>);
static_assert(std::is_same_v<decltype(WorldPlayerState::side), model::TeamSide>);
static_assert(std::is_same_v<decltype(TacticalBoard::side), model::TeamSide>);

template<class T> concept HasRuntimeIndex =
    requires { &T::GetIndex; } || requires { &T::index; };
static_assert(!HasRuntimeIndex<model::Player>);
static_assert(!HasRuntimeIndex<WorldPlayerState>);

namespace {

void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

bool SameVector(const blunted::Vector3& a, const blunted::Vector3& b) {
  return std::memcmp(a.coords, b.coords, sizeof(a.coords)) == 0;
}

void CheckSamePhysics(Simulation& a, Simulation& b) {
  const WorldState x = a.Observe(), y = b.Observe();
  Require(x.tick == y.tick && SameVector(x.ball_position, y.ball_position) &&
              x.players.size() == y.players.size(),
          "identity changed world timing or ball physics");
  for (std::size_t i = 0; i < x.players.size(); ++i) {
    const auto& p = x.players[i];
    const auto& q = y.players[i];
    Require(p.side == q.side && p.active == q.active &&
                p.has_possession == q.has_possession &&
                SameVector(p.position, q.position) &&
                SameVector(p.velocity, q.velocity) && SameVector(p.facing, q.facing),
            "identity changed player physics");
  }
  Require(a.match()->rng().engine() == b.match()->rng().engine(),
          "identity changed RNG order");
  for (int side = 0; side < 2; ++side) {
    std::vector<Player*> left, right;
    a.match()->GetTeam(side)->GetAllPlayers(left);
    b.match()->GetTeam(side)->GetAllPlayers(right);
    for (std::size_t i = 0; i < left.size(); ++i) {
      const auto& p = left[i]->GetSimulationActionState();
      const auto& q = right[i]->GetSimulationActionState();
      Require(p.type == q.type && p.frame == q.frame && p.frameCount == q.frameCount &&
                  p.elapsedTime_ms == q.elapsedTime_ms &&
                  p.durationTime_ms == q.durationTime_ms &&
                  p.contactTime_ms == q.contactTime_ms &&
                  SameVector(p.contactPosition, q.contactPosition) &&
                  left[i]->GetTimeNeededToGetToBall_ms() ==
                      right[i]->GetTimeNeededToGetToBall_ms() &&
                  left[i]->GetFatigueFactorInv() == right[i]->GetFatigueFactorInv(),
              "identity changed action or staggered reachability state");
      const auto& u = left[i]->GetTacticalSituation();
      const auto& v = right[i]->GetTacticalSituation();
      Require(u.forwardSpaceRating == v.forwardSpaceRating &&
                  u.toGoalSpaceRating == v.toGoalSpaceRating &&
                  u.spaceRating == v.spaceRating && u.forwardRating == v.forwardRating,
              "identity changed staggered tactical updates");
    }
  }
}

void CheckIdentity(Simulation& simulation, const model::Team& home,
                   const model::Team& away) {
  const WorldState world = simulation.Observe();
  std::array<bool, 256> seen{};
  std::size_t cursor = 0;
  Match* match = simulation.match();
  const std::size_t first_count =
      match->GetMatchData()->GetTeamData(match->FirstTeam()).GetPlayerNum();
  for (int side = 0; side < 2; ++side) {
    std::vector<Player*> players;
    match->GetTeam(side)->GetAllPlayers(players);
    const model::Team& description = side == 0 ? home : away;
    const auto team_side = side == 0 ? model::TeamSide::Home : model::TeamSide::Away;
    for (std::size_t i = 0; i < players.size(); ++i) {
      const std::size_t index = (side == match->FirstTeam() ? 0 : first_count) + i;
      Player* player = players[i];
      Require(player->GetIndex() == index && !seen.at(index),
              "execution indices are not dense in construction order");
      seen[index] = true;
      Require(player->GetID() == description.players.at(i).id &&
                  player->GetPlayerData()->GetModel().id == player->GetID() &&
                  world.players.at(cursor).id == player->GetID() &&
                  world.players.at(cursor).side == team_side,
              "model identity/side was replaced with an execution ordinal");
      ++cursor;
    }
  }
  Require(cursor == world.players.size(), "missing world player identity");
  for (std::size_t i = 0; i < cursor; ++i) Require(seen[i], "index has a hole");
}

void CheckIdentityDoesNotDriveSimulation(bool reverse) {
  const auto default_home = football::data::MakeDefaultHomeTeam();
  const auto default_away = football::data::MakeDefaultAwayTeam();
  auto home = default_home, away = default_away;
  for (std::size_t i = 0; i < home.players.size(); ++i) {
    home.players[i].id = 4000000033u - static_cast<model::PlayerId>(i) * 17u;
    away.players[i].id = 2000000003u + static_cast<model::PlayerId>(i) * 19u;
  }
  home.players[0].id = model::kInvalidPlayerId - 1;
  away.players[0].id = 0;  // Valid identity, but not this actor's execution index.
  MatchOptions options;
  options.reverse_team_processing = reverse;
  Simulation reference, renamed;  // No GameEnv/GetContext needed.
  reference.Init(default_home, default_away, model::MakeLegacyPitch(), options, false);
  renamed.Init(home, away, model::MakeLegacyPitch(), options, false);
  CheckIdentity(renamed, home, away);

  PlayerControl move;
  move.move_direction = blunted::Vector3(0, 1, 0);
  move.desired_speed = 2.345f;
  PlayerControlSet reference_controls, renamed_controls;
  reference_controls.Set(default_away.players[0].id, move);
  renamed_controls.Set(away.players[0].id, move);
  // This was a legacy ordinal. No model player has ID 1, so it must not bind.
  PlayerControl wrong;
  wrong.action = ControlAction::Shoot;
  wrong.power = 0.987f;
  renamed_controls.Set(1, wrong);
  const WorldState initial = renamed.Observe();
  for (int tick = 0; tick < 600; ++tick) {
    reference.Step(reference_controls);
    renamed.Step(renamed_controls);
    CheckSamePhysics(reference, renamed);
  }
  std::vector<Player*> players;
  renamed.match()->GetTeam(1)->GetAllPlayers(players);
  PlayerCommandQueue commands;
  players[0]->RequestCommand(commands);
  Require(commands.size() == 1 && commands[0].desiredVelocityFloat == move.desired_speed,
          "controls did not bind by model identity");
  const WorldState replay_target = renamed.Observe();

  // Indices are fixed for the whole match, not compacted on a send-off.
  players.clear();
  reference.match()->GetTeam(0)->GetAllPlayers(players);
  players[1]->SendOff();
  players.clear();
  renamed.match()->GetTeam(0)->GetAllPlayers(players);
  players[1]->SendOff();
  CheckIdentity(renamed, home, away);
  CheckSamePhysics(reference, renamed);

  renamed.Stop();
  renamed.Init(home, away, model::MakeLegacyPitch(), options, false);
  CheckIdentity(renamed, home, away);
  Require(renamed.Observe().players[0].id == initial.players[0].id,
          "reset reassigned model identity");
  for (int tick = 0; tick < 600; ++tick) renamed.Step(renamed_controls);
  const WorldState replay = renamed.Observe();
  for (std::size_t i = 0; i < replay.players.size(); ++i) {
    Require(replay.players[i].id == replay_target.players[i].id &&
                replay.players[i].side == replay_target.players[i].side &&
                SameVector(replay.players[i].position, replay_target.players[i].position),
            "identity/control replay changed");
  }
}

void CheckRosterIndexing() {
  auto home = football::data::MakeDefaultHomeTeam();
  auto away = football::data::MakeDefaultAwayTeam();
  home.players.resize(3);
  away.players.resize(2);
  home.formation.resize(3);
  away.formation.resize(2);
  for (bool reverse : {false, true}) {
    MatchOptions options;
    options.reverse_team_processing = reverse;
    Simulation simulation;
    simulation.Init(home, away, model::MakeLegacyPitch(), options, false);
    CheckIdentity(simulation, home, away);
    simulation.Stop();
    std::swap(home.players[0], home.players[2]);
    simulation.Init(home, away, model::MakeLegacyPitch(), options, false);
    CheckIdentity(simulation, home, away);  // Identity follows the model, not slot.
  }

  // Home/away is a match role, not identity of the team/profile database.
  Simulation swapped;
  swapped.Init(away, home, model::MakeLegacyPitch(), MatchOptions{}, false);
  CheckIdentity(swapped, away, home);

  // Exercise the last representable index, including inactive roster entries.
  home.players.resize(128, home.players[0]);
  away.players.resize(128, away.players[0]);
  home.formation.resize(128);
  away.formation.resize(128);
  for (std::size_t i = 0; i < 128; ++i) {
    home.players[i].id = 1000u + static_cast<model::PlayerId>(i);
    away.players[i].id = 2000u + static_cast<model::PlayerId>(i);
  }
  Simulation capacity;
  capacity.Init(home, away, model::MakeLegacyPitch(), MatchOptions{}, false);
  CheckIdentity(capacity, home, away);
}

void CheckValidationAndDefaults() {
  const auto home = football::data::MakeDefaultHomeTeam();
  const auto away = football::data::MakeDefaultAwayTeam();
  Require(home.players[0].id == 0 && away.players[0].id == 11 &&
              home.players[0].database_id == away.players[0].database_id,
          "default identity is conflated with database provenance");
  Require(football::data::LoadLegacyPlayerProfile(398, true).id == model::kInvalidPlayerId,
          "profile loader invented a player identity from provenance");
  Simulation reference, fallback;
  reference.Init(home, away, model::MakeLegacyPitch(), MatchOptions{}, false);
  fallback.Init(model::Team{}, model::Team{}, model::MakeLegacyPitch(), MatchOptions{}, false);
  CheckIdentity(fallback, home, away);
  CheckSamePhysics(reference, fallback);
  auto& rng = reference.match()->rng();
  reference.Stop();
  const auto rng_before = rng.engine();
  const auto reject = [&](const model::Team& h, const model::Team& a) {
    bool rejected = false;
    try {
      reference.Init(h, a, model::MakeLegacyPitch(), MatchOptions{}, false);
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    Require(rejected && reference.match() == nullptr && rng.engine() == rng_before,
            "invalid identity/index input was accepted or consumed RNG");
  };
  auto invalid = home;
  invalid.players[0].id = model::kInvalidPlayerId;
  reject(invalid, away);
  invalid = home;
  invalid.players[1].id = invalid.players[0].id;
  reject(invalid, away);
  reject(home, home);  // Duplicate across opponents, despite distinct sides.
  invalid = home;
  invalid.players[0].id = away.players[0].id;
  reject(invalid, model::Team{});  // Includes the resolved default roster.
  invalid = home;
  invalid.players.resize(3);  // Empty formation still requires 11 profiles.
  reject(invalid, away);
  invalid = home;
  invalid.formation.resize(257);
  reject(invalid, away);
  reference.Init(home, away, model::MakeLegacyPitch(), MatchOptions{}, false);
  CheckIdentity(reference, home, away);  // A failed Init remains reusable.
}

}  // namespace

int main() {
  try {
    CheckValidationAndDefaults();
    CheckRosterIndexing();
    CheckIdentityDoesNotDriveSimulation(false);
    CheckIdentityDoesNotDriveSimulation(true);
    std::cout << "football_player_identity_test: PASS\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "football_player_identity_test: FAIL: " << error.what() << '\n';
    return 1;
  }
}
