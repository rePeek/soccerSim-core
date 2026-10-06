#include <array>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "ai/tactical_board.hpp"
#include "app/fixtures/default_teams.hpp"
#include "app/fixtures/legacy_player_profile.hpp"
#include "sim/match.hpp"
#include "sim/player/player.hpp"
#include "sim/simulation.hpp"
#include "../test/default_ai_fixture.hpp"

namespace model = football::model;
static_assert(std::is_same_v<model::PlayerId, std::uint32_t>);
static_assert(std::is_same_v<decltype(WorldPlayerState::side), model::TeamSide>);
static_assert(std::is_same_v<decltype(TacticalBoard::side), model::TeamSide>);

template<class T> concept HasRuntimeIndex =
    requires { &T::GetIndex; } || requires { &T::index; };
static_assert(!HasRuntimeIndex<model::Player>);
static_assert(!HasRuntimeIndex<WorldPlayerState>);
static_assert(!HasRuntimeIndex<Player>);
template<class T> concept HasSchedulingDetail =
    requires(T& value) { value.schedule_phase_; } ||
    requires { &T::GetSchedulePhase; };
static_assert(!HasSchedulingDetail<model::Player>);
static_assert(!HasSchedulingDetail<WorldPlayerState>);
static_assert(!HasSchedulingDetail<Player>);

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
  std::size_t cursor = 0;
  Match* match = simulation.match();
  for (int side = 0; side < 2; ++side) {
    std::vector<Player*> players;
    match->GetTeam(side)->GetAllPlayers(players);
    const model::Team& description = side == 0 ? home : away;
    const auto team_side = side == 0 ? model::TeamSide::Home : model::TeamSide::Away;
    for (std::size_t i = 0; i < players.size(); ++i) {
      Player* player = players[i];
      Require(player->GetID() == description.players.at(i).id &&
                  player->GetModel().id == player->GetID() &&
                  world.players.at(cursor).id == player->GetID() &&
                  world.players.at(cursor).side == team_side,
              "model identity/side was replaced with an execution ordinal");
      ++cursor;
    }
  }
  Require(cursor == world.players.size(), "missing world player identity");
}

void CheckIdentityDoesNotDriveSimulation(bool reverse) {
  const auto default_home = football::app::fixtures::MakeDefaultHomeTeam();
  const auto default_away = football::app::fixtures::MakeDefaultAwayTeam();
  auto home = default_home, away = default_away;
  for (std::size_t i = 0; i < home.players.size(); ++i) {
    home.players[i].id = 4000000033u - static_cast<model::PlayerId>(i) * 17u;
    away.players[i].id = 2000000003u + static_cast<model::PlayerId>(i) * 19u;
  }
  home.players[0].id = model::kInvalidPlayerId - 1;
  away.players[0].id = 0;  // Zero is a valid identity, not a scheduler phase.
  MatchOptions options;
  options.reverse_team_processing = reverse;
  Simulation reference, renamed;  // Independent runtimes, no decision owners.
  reference.Init(default_home, default_away, model::MakeLegacyPitch(), options, false);
  renamed.Init(home, away, model::MakeLegacyPitch(), options, false);
  CheckIdentity(renamed, home, away);
  const auto reference_ai = football::test::MakeDefaultAI(reference);
  const auto renamed_ai = football::test::MakeDefaultAI(renamed);

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
    football::test::StepDefaultAI(reference, reference_ai, reference_controls);
    football::test::StepDefaultAI(renamed, renamed_ai, renamed_controls);
    CheckSamePhysics(reference, renamed);
  }
  std::vector<Player*> players;
  renamed.match()->GetTeam(1)->GetAllPlayers(players);
  PlayerCommandQueue commands;
  players[0]->RequestCommand(commands);
  Require(commands.size() == 1 && commands[0].desiredVelocityFloat == move.desired_speed,
          "controls did not bind by model identity");
  const WorldState replay_target = renamed.Observe();

  // A send-off must not change other actors' scheduling or model identities.
  players.clear();
  reference.match()->GetTeam(0)->GetAllPlayers(players);
  players[1]->SendOff();
  players.clear();
  renamed.match()->GetTeam(0)->GetAllPlayers(players);
  players[1]->SendOff();
  CheckIdentity(renamed, home, away);
  CheckSamePhysics(reference, renamed);
  for (int tick = 0; tick < 200; ++tick) {
    football::test::StepDefaultAI(reference, reference_ai, reference_controls);
    football::test::StepDefaultAI(renamed, renamed_ai, renamed_controls);
    CheckSamePhysics(reference, renamed);
  }

  renamed.Stop();
  renamed.Init(home, away, model::MakeLegacyPitch(), options, false);
  CheckIdentity(renamed, home, away);
  Require(renamed.Observe().players[0].id == initial.players[0].id,
          "reset reassigned model identity");
  for (int tick = 0; tick < 600; ++tick) football::test::StepDefaultAI(renamed, renamed_ai, renamed_controls);
  const WorldState replay = renamed.Observe();
  for (std::size_t i = 0; i < replay.players.size(); ++i) {
    Require(replay.players[i].id == replay_target.players[i].id &&
                replay.players[i].side == replay_target.players[i].side &&
                SameVector(replay.players[i].position, replay_target.players[i].position),
            "identity/control replay changed");
  }
}

void CheckRosterComposition() {
  auto home = football::app::fixtures::MakeDefaultHomeTeam();
  auto away = football::app::fixtures::MakeDefaultAwayTeam();
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

  // Scheduler phases repeat; their byte storage is not a roster-size limit.
  // Include inactive entries and cross the retired 256-actor boundary.
  home.players.resize(130, home.players[0]);
  away.players.resize(130, away.players[0]);
  home.formation.resize(130);
  away.formation.resize(130);
  for (std::size_t i = 0; i < 130; ++i) {
    home.players[i].id = 1000u + static_cast<model::PlayerId>(i);
    away.players[i].id = 2000u + static_cast<model::PlayerId>(i);
  }
  for (bool reverse : {false, true}) {
    MatchOptions options;
    options.reverse_team_processing = reverse;
    Simulation large;
    large.Init(home, away, model::MakeLegacyPitch(), options, false);
    CheckIdentity(large, home, away);
    Require(large.Observe().players.size() == 260, "phase storage limited roster size");
    const auto policy = football::test::MakeDefaultAI(large);
    for (int tick = 0; tick < 350; ++tick) football::test::StepDefaultAI(large, policy);
    CheckIdentity(large, home, away);
  }
}

void CheckValidationAndDefaults() {
  const auto home = football::app::fixtures::MakeDefaultHomeTeam();
  const auto away = football::app::fixtures::MakeDefaultAwayTeam();
  Require(home.players[0].id == 0 && away.players[0].id == 11 &&
              home.players[0].database_id == away.players[0].database_id,
          "default identity is conflated with database provenance");
  Require(football::app::fixtures::LoadLegacyPlayerProfile(398, true).id == model::kInvalidPlayerId,
          "profile loader invented a player identity from provenance");
  Simulation reference;
  reference.Init(home, away, model::MakeLegacyPitch(), MatchOptions{}, false);
  CheckIdentity(reference, home, away);
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
            "invalid identity/formation input was accepted or consumed RNG");
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
  reject(invalid, model::Team{});  // An empty roster is no longer a default.
  invalid = home;
  invalid.players.resize(3);  // Empty formation still requires 11 profiles.
  reject(invalid, away);
  invalid = home;
  invalid.formation.resize(257);
  reject(invalid, away);
  reference.Init(home, away, model::MakeLegacyPitch(), MatchOptions{}, false);
  CheckIdentity(reference, home, away);  // A failed Init remains reusable.
}

// Policy-versioned fingerprints include caches, motion/actions and RNG.
// Previous Eliza fingerprints are archived in test/baselines/pre_value_ai.md.
std::uint64_t CaptureScheduleState(Simulation& simulation) {
  std::uint64_t hash = UINT64_C(1469598103934665603);
  const auto bytes = [&](const void* data, std::size_t size) {
    const auto* p = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
      hash ^= p[i];
      hash *= UINT64_C(1099511628211);
    }
  };
  const auto value = [&](const auto& v) { bytes(&v, sizeof(v)); };
  const WorldState world = simulation.Observe();
  value(world.tick);
  bytes(world.ball_position.coords, sizeof(world.ball_position.coords));
  for (const auto& p : world.players) {
    bytes(p.position.coords, sizeof(p.position.coords));
    bytes(p.velocity.coords, sizeof(p.velocity.coords));
    bytes(p.facing.coords, sizeof(p.facing.coords));
    value(p.active);
    value(p.has_possession);
  }
  for (int side = 0; side < 2; ++side) {
    for (Player* p : simulation.match()->GetTeam(side)->GetAllPlayers()) {
      const auto& action = p->GetSimulationActionState();
      value(action.type);
      value(action.frame);
      value(action.frameCount);
      value(action.elapsedTime_ms);
      value(action.contactTime_ms);
      value(p->GetTimeNeededToGetToBall_ms());
      value(p->GetTimeNeededToGetToBall_previous_ms());
      value(p->GetFatigueFactorInv());
      const auto& tactics = p->GetTacticalSituation();
      value(tactics.forwardSpaceRating);
      value(tactics.toGoalSpaceRating);
      value(tactics.spaceRating);
      value(tactics.forwardRating);
    }
  }
  std::ostringstream rng;
  rng << simulation.match()->rng().engine();
  const auto state = rng.str();
  bytes(state.data(), state.size());
  return hash;
}

void CheckHistoricalScheduling(bool print_baseline) {
  struct Case {
    bool unequal;
    bool reverse;
    std::uint64_t before_send_off;
    std::uint64_t after_send_off;
  };
  constexpr Case cases[] = {
      {false, false, UINT64_C(13711565807151734712), UINT64_C(17201809211116718618)},
      {false, true, UINT64_C(16717170963448916555), UINT64_C(13813028668665107254)},
      {true, false, UINT64_C(1658563135750430224), UINT64_C(10374616427843446362)},
      {true, true, UINT64_C(13287372985434084791), UINT64_C(12082599955711407290)},
  };
  for (const Case& test : cases) {
    auto home = football::app::fixtures::MakeDefaultHomeTeam();
    auto away = football::app::fixtures::MakeDefaultAwayTeam();
    if (test.unequal) {
      home.players.resize(3);
      away.players.resize(2);
      home.formation.resize(3);
      away.formation.resize(2);
    }
    MatchOptions options;
    options.reverse_team_processing = test.reverse;
    Simulation simulation;
    simulation.Init(home, away, model::MakeLegacyPitch(), options, false);
    const auto policy = football::test::MakeDefaultAI(simulation);
    for (int tick = 0; tick < 300; ++tick) football::test::StepDefaultAI(simulation, policy);
    const auto before = CaptureScheduleState(simulation);
    simulation.match()->GetTeam(0)->GetAllPlayers().at(1)->SendOff();
    for (int tick = 0; tick < 300; ++tick) football::test::StepDefaultAI(simulation, policy);
    const auto after = CaptureScheduleState(simulation);
    if (print_baseline) {
      std::cout << "    {" << test.unequal << ", " << test.reverse << ", UINT64_C("
                << before << "), UINT64_C(" << after << ")},\n";
    } else {
      Require(before == test.before_send_off && after == test.after_send_off,
              "value-policy scheduling/send-off fingerprint changed");
    }
  }
}

class QuietInput final : public ControllerInput {
 public:
  bool GetButton(e_ButtonFunction) override { return false; }
  bool GetPreviousButtonState(e_ButtonFunction) override { return false; }
  blunted::Vector3 GetDirection() override { return blunted::Vector3(0); }
  blunted::Vector3 GetOriginalDirection() override { return blunted::Vector3(0); }
  bool Disabled() const override { return false; }
  void ResetNotSticky() override {}
  void Mirror(float) override {}
  int GetPlayerColorIndex() const override { return 0; }
};

void CheckControllerRosterOrder() {
  for (bool reverse : {false, true}) {
    for (int side : {0, 1}) {
      auto home = football::app::fixtures::MakeDefaultHomeTeam();
      auto away = football::app::fixtures::MakeDefaultAwayTeam();
      for (std::size_t i = 0; i < home.players.size(); ++i) {
        home.players[i].id = 4000000000u - static_cast<model::PlayerId>(i);
        away.players[i].id = 2000000000u - static_cast<model::PlayerId>(i);
      }
      std::array<QuietInput, 4> inputs;  // Outlive all HumanGamer readers.
      Simulation simulation;
      MatchOptions options;
      options.reverse_team_processing = reverse;
      simulation.Init(home, away, model::MakeLegacyPitch(), options, false);
      Team* team = simulation.match()->GetTeam(side);
      const auto& roster = team->GetAllPlayers();
      for (std::size_t i = 0; i < roster.size(); ++i) {
        auto entry = roster[i]->GetFormationEntry();
        entry.controllable = i == 0 || i == 1 || i == 10;
        team->SetFormationEntry(roster[i], entry);
        roster[i]->ResetPosition(blunted::Vector3(20.0f + i, 0, 0),
                                 blunted::Vector3(0));
      }
      const auto ball = simulation.match()->GetBall()->Predict(0).Get2D();
      roster[10]->ResetPosition(ball + blunted::Vector3(0.1f, 0, 0), ball);
      roster[1]->ResetPosition(ball + blunted::Vector3(0.2f, 0, 0), ball);
      roster[0]->ResetPosition(ball + blunted::Vector3(0.3f, 0, 0), ball);
      team->AddHumanGamers({&inputs[0], &inputs[1], &inputs[2], &inputs[3]});
      std::vector<HumanGamer*> controllers;
      team->GetHumanControllers(controllers);
      // Slots 0 and 10 share a phase, IDs run backwards, and closest order is
      // 10,1,0. Only roster order can produce the required mapping 0,1,10.
      Require(controllers.size() == 4 &&
                  controllers[0]->GetSelectedPlayer() == roster[0] &&
                  controllers[1]->GetSelectedPlayer() == roster[1] &&
                  controllers[2]->GetSelectedPlayer() == roster[10] &&
                  controllers[3]->GetSelectedPlayer() == nullptr &&
                  team->MainSelectedPlayer() == roster[10],
              "controller binding used IDs/phases/distance instead of roster order");
    }
  }
}

}  // namespace

int main(int argc, char **argv) {
  try {
    CheckValidationAndDefaults();
    CheckRosterComposition();
    CheckHistoricalScheduling(argc == 2 && std::string(argv[1]) == "--print-baseline");
    CheckControllerRosterOrder();
    CheckIdentityDoesNotDriveSimulation(false);
    CheckIdentityDoesNotDriveSimulation(true);
    std::cout << "football_player_identity_test: PASS\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "football_player_identity_test: FAIL: " << error.what() << '\n';
    return 1;
  }
}
