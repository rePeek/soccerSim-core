#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>

#include "game_env.hpp"
#include "onthepitch/player/player_kinematics.hpp"
#include "onthepitch/player/player_ground_collider.hpp"
#include "onthepitch/player/player_action_executor.hpp"
#include "onthepitch/player/player_action_volume.hpp"

namespace {

constexpr float kFloatTolerance = 1e-5f;

struct RegressionFailure : std::exception {
  explicit RegressionFailure(const std::string& message) : message(message) {}
  const char* what() const noexcept override { return message.c_str(); }
  std::string message;
};

void Require(bool condition, const std::string& message) {
  if (!condition) throw RegressionFailure(message);
}

void RequireNear(float actual, float expected, const std::string& label) {
  if (std::fabs(actual - expected) > kFloatTolerance) {
    std::ostringstream message;
    message << label << ": expected " << expected << ", got " << actual;
    throw RegressionFailure(message.str());
  }
}

void RequirePositionNear(const Position& actual, const Position& expected,
                         const std::string& label) {
  for (int axis = 0; axis < 3; ++axis) {
    RequireNear(actual.env_coord(axis), expected.env_coord(axis),
                label + "[" + std::to_string(axis) + "]");
  }
}

void RequireInfoEqual(const SharedInfo& actual, const SharedInfo& expected,
                      const std::string& label) {
  Require(actual.step == expected.step, label + ": step differs");
  Require(actual.left_goals == expected.left_goals,
          label + ": left score differs");
  Require(actual.right_goals == expected.right_goals,
          label + ": right score differs");
  Require(actual.game_mode == expected.game_mode, label + ": game mode differs");
  Require(actual.is_in_play == expected.is_in_play,
          label + ": in-play state differs");
  Require(actual.ball_owned_team == expected.ball_owned_team,
          label + ": ball owner team differs");
  Require(actual.ball_owned_player == expected.ball_owned_player,
          label + ": ball owner player differs");
  RequirePositionNear(actual.ball_position, expected.ball_position,
                      label + ": ball position");
  RequirePositionNear(actual.ball_direction, expected.ball_direction,
                      label + ": ball direction");
  RequirePositionNear(actual.ball_rotation, expected.ball_rotation,
                      label + ": ball rotation");

  const auto compare_players = [&](const std::vector<PlayerInfo>& current,
                                   const std::vector<PlayerInfo>& reference,
                                   const char* team_name) {
    Require(current.size() == reference.size(),
            label + ": " + team_name + " player count differs");
    for (size_t index = 0; index < current.size(); ++index) {
      const PlayerInfo& player = current[index];
      const PlayerInfo& expected_player = reference[index];
      const std::string player_label = label + ": " + team_name +
          " player " + std::to_string(index);
      RequirePositionNear(player.player_position, expected_player.player_position,
                          player_label + " position");
      RequirePositionNear(player.player_direction, expected_player.player_direction,
                          player_label + " direction");
      RequireNear(player.tired_factor, expected_player.tired_factor,
                  player_label + " tired factor");
      Require(player.has_card == expected_player.has_card,
              player_label + " card differs");
      Require(player.is_active == expected_player.is_active,
              player_label + " active state differs");
      Require(player.designated_player == expected_player.designated_player,
              player_label + " designated state differs");
      Require(player.role == expected_player.role, player_label + " role differs");
    }
  };

  compare_players(actual.left_team, expected.left_team, "left");
  compare_players(actual.right_team, expected.right_team, "right");
  Require(actual.left_controllers == expected.left_controllers,
          label + ": left controllers differ");
  Require(actual.right_controllers == expected.right_controllers,
          label + ": right controllers differ");
}

uint64_t HashBytes(uint64_t hash, const void* bytes, size_t size) {
  const auto* value = static_cast<const unsigned char*>(bytes);
  for (size_t index = 0; index < size; ++index) {
    hash ^= value[index];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

template <typename T>
uint64_t HashValue(uint64_t hash, const T& value) {
  return HashBytes(hash, &value, sizeof(value));
}

uint64_t HashPosition(uint64_t hash, const Position& position) {
  for (int axis = 0; axis < 3; ++axis) {
    const float value = position.env_coord(axis);
    hash = HashValue(hash, value);
  }
  return hash;
}

uint64_t HashInfo(const SharedInfo& info) {
  uint64_t hash = UINT64_C(1469598103934665603);
  hash = HashPosition(hash, info.ball_position);
  hash = HashPosition(hash, info.ball_direction);
  hash = HashPosition(hash, info.ball_rotation);
  hash = HashValue(hash, info.left_goals);
  hash = HashValue(hash, info.right_goals);
  hash = HashValue(hash, info.game_mode);
  hash = HashValue(hash, info.is_in_play);
  hash = HashValue(hash, info.ball_owned_team);
  hash = HashValue(hash, info.ball_owned_player);
  hash = HashValue(hash, info.step);

  const auto hash_players = [&](const std::vector<PlayerInfo>& players) {
    uint64_t result = HashValue(hash, players.size());
    for (const PlayerInfo& player : players) {
      result = HashPosition(result, player.player_position);
      result = HashPosition(result, player.player_direction);
      result = HashValue(result, player.has_card);
      result = HashValue(result, player.is_active);
      result = HashValue(result, player.designated_player);
      result = HashValue(result, player.tired_factor);
      result = HashValue(result, player.role);
    }
    return result;
  };
  hash = hash_players(info.left_team);
  hash = hash_players(info.right_team);

  const auto hash_controllers = [&](const std::vector<ControllerInfo>& controllers) {
    uint64_t result = HashValue(hash, controllers.size());
    for (const ControllerInfo& controller : controllers) {
      result = HashValue(result, controller.controlled_player);
    }
    return result;
  };
  hash = hash_controllers(info.left_controllers);
  hash = hash_controllers(info.right_controllers);
  return hash;
}

void CheckPlayerKinematics() {
  PlayerKinematicState state;
  PlayerKinematicParameters parameters;
  parameters.maxSpeed = 10.0f;
  parameters.acceleration = 10.0f;
  parameters.braking = 5.0f;
  parameters.maxTurnRate = 1.0f;

  PlayerKinematicInput input;
  input.desiredVelocity = Vector3(10.0f, 0.0f, 0.0f);
  input.desiredFacing = Vector3(1.0f, 0.0f, 0.0f);
  PlayerKinematics::Step(state, input, parameters, 0.1f);

  RequireNear(state.velocity.coords[0], 1.0f,
              "kinematics acceleration velocity");
  RequireNear(state.position.coords[0], 0.1f,
              "kinematics acceleration position");
  RequireNear(state.speed, 1.0f, "kinematics acceleration speed");
  Require(state.facing.GetDotProduct(input.desiredFacing) > 0.0f,
          "kinematics should turn toward the desired facing");

  input.desiredVelocity = Vector3(0);
  PlayerKinematics::Step(state, input, parameters, 0.1f);
  RequireNear(state.velocity.coords[0], 0.5f,
              "kinematics braking velocity");
  RequireNear(state.position.coords[0], 0.15f,
              "kinematics braking position");
  RequireNear(state.velocity.coords[2], 0.0f,
              "kinematics planar velocity");
  RequireNear(state.position.coords[2], 0.0f,
              "kinematics planar position");
}

void CheckPlayerKinematicMirror() {
  PlayerKinematicState state;
  state.position = Vector3(1.0f, 2.0f, 0.0f);
  state.velocity = Vector3(3.0f, 4.0f, 0.0f);
  state.facing = Vector3(0.6f, 0.8f, 0.0f);
  state.speed = 5.0f;

  state.Mirror();
  RequireNear(state.position.coords[0], -1.0f, "mirror position x");
  RequireNear(state.position.coords[1], -2.0f, "mirror position y");
  RequireNear(state.velocity.coords[0], -3.0f, "mirror velocity x");
  RequireNear(state.velocity.coords[1], -4.0f, "mirror velocity y");
  // Legacy spatial-state mirroring negates position and movement only, so a
  // mirrored kinematic state must keep facing untouched.
  RequireNear(state.facing.coords[0], 0.6f, "mirror facing x");
  RequireNear(state.facing.coords[1], 0.8f, "mirror facing y");
  RequireNear(state.speed, 5.0f, "mirror speed");
}

void CheckPlayerGroundCollider() {
  PlayerGroundCollider first;
  first.SetCenter(Vector3(0.0f, 0.0f, 2.0f));
  RequireNear(first.center.coords[2], 0.0f,
              "ground collider should stay on the pitch plane");

  PlayerGroundCollider second;
  second.SetCenter(Vector3(0.71f, 0.0f, 0.0f));
  Require(first.Intersects(second),
          "ground colliders should intersect within their radii");

  second.SetCenter(Vector3(0.72f, 0.0f, 0.0f));
  Require(!first.Intersects(second),
          "ground colliders should not intersect at the radius boundary");
}

void CheckPlayerActionExecutor() {
  PlayerActionDefinition definition;
  definition.type = e_FunctionType_Shot;
  definition.durationTime_ms = 300;
  definition.contactTime_ms = 120;
  definition.contactPosition = Vector3(1.0f, -2.0f, 0.5f);

  PlayerActionState state;
  PlayerActionExecutor::Begin(state, definition);
  Require(state.type == e_FunctionType_Shot, "action executor type");
  Require(state.frame == 0 && state.frameCount == 30,
          "action executor initial frames");
  Require(state.elapsedTime_ms == 0 && state.durationTime_ms == 300,
          "action executor initial timing");
  Require(state.contactFrame == 12 && state.contactTime_ms == 120,
          "action executor contact timing");
  RequireNear(state.contactPosition.coords[0], 1.0f,
              "action executor contact position x");
  Require(state.IsContactPending() && !state.IsContactDue() &&
              !state.IsComplete(),
          "action executor initial phase");

  const PlayerActionStepResult beforeContact =
      PlayerActionExecutor::Step(state, 100);
  Require(state.elapsedTime_ms == 100 && state.frame == 10 &&
              state.IsContactPending(),
          "action executor pre-contact phase");
  Require(!beforeContact.contactTriggered && !beforeContact.completed,
          "action executor should not emit an early event");

  const PlayerActionStepResult atContact =
      PlayerActionExecutor::Step(state, 20);
  Require(state.elapsedTime_ms == 120 && state.frame == 12 &&
              !state.IsContactPending() && state.IsContactDue(),
          "action executor contact phase");
  Require(atContact.contactTriggered && !atContact.completed,
          "action executor should emit one contact event");

  const PlayerActionStepResult atCompletion =
      PlayerActionExecutor::Step(state, 500);
  Require(state.elapsedTime_ms == 300 && state.frame == 30 &&
              state.IsComplete(),
          "action executor completion phase");
  Require(!atCompletion.contactTriggered && atCompletion.completed,
          "action executor should emit one completion event");
}

void CheckPlayerActionVolume() {
  PlayerActionState action;
  PlayerKinematicState kinematics;
  kinematics.position = Vector3(0.0f, 0.0f, 0.0f);
  kinematics.facing = Vector3(0.0f, -1.0f, 0.0f);

  PlayerActionVolumeParameters parameters;
  parameters.contactWindowStartFrame = 5;
  parameters.contactWindowEndFrame = 28;

  // Non-reaching actions never produce a volume.
  action.type = e_FunctionType_Movement;
  action.frame = 10;
  Require(!BuildTackleVolume(action, kinematics, parameters).active,
          "movement should not have a tackle volume");

  // Outside the contact window the volume is inactive too.
  action.type = e_FunctionType_Sliding;
  action.frame = 5;
  Require(!BuildTackleVolume(action, kinematics, parameters).active,
          "slide before the contact window should be inactive");
  action.frame = 28;
  Require(!BuildTackleVolume(action, kinematics, parameters).active,
          "slide after the contact window should be inactive");

  // Inside the window a slide reaches forward along facing.
  action.frame = 12;
  const PlayerActionVolume slide =
      BuildTackleVolume(action, kinematics, parameters);
  Require(slide.active, "slide inside the contact window should be active");
  RequireNear(slide.axis.coords[1], -1.0f, "slide axis should follow facing y");

  PlayerGroundCollider victim;
  const float reach = parameters.slideReach + parameters.slideRadius +
                      victim.radius;
  victim.SetCenter(Vector3(0.0f, -1.0f, 0.0f));
  Require(slide.Intersects(victim), "slide should reach a victim in front");

  // The capsule starts at the actor, so a victim behind is not reached.
  victim.SetCenter(Vector3(0.0f, 1.0f, 0.0f));
  Require(!slide.Intersects(victim), "slide should not reach behind");

  // Reach plus radii, inclusive boundary.
  victim.SetCenter(Vector3(0.0f, -(reach - 0.01f), 0.0f));
  Require(slide.Intersects(victim), "slide should reach just inside its boundary");
  victim.SetCenter(Vector3(0.0f, -(reach + 0.01f), 0.0f));
  Require(!slide.Intersects(victim), "slide should not reach past its boundary");

  // Standing tackle is shorter and broader.
  action.type = e_FunctionType_Interfere;
  const PlayerActionVolume interfere =
      BuildTackleVolume(action, kinematics, parameters);
  Require(interfere.active, "interfere should be active inside the window");
  Require(interfere.length < slide.length,
          "interfere should reach less far than a slide");
  Require(interfere.radius > slide.radius,
          "interfere should be broader than a slide");

  // Height is ignored: everything is evaluated on the pitch plane.
  victim.SetCenter(Vector3(0.0f, -1.0f, 2.0f));
  Require(slide.Intersects(victim), "tackle volume should be planar");
}

ScenarioConfig MakeBuiltinAiConfig() {
  auto config = ScenarioConfig::make();
  config->left_agents = 0;
  config->right_agents = 0;
  config->real_time = false;
  config->deterministic = true;
  config->game_engine_random_seed = 42;
  return *config;
}

void Advance(GameEnv& env, int steps) {
  for (int step = 0; step < steps; ++step) env.step();
}

void WaitUntilInPlay(GameEnv& env, int max_steps, const std::string& label) {
  for (int step = 0; step < max_steps; ++step) {
    if (env.context->gameTask->GetMatch()->IsInPlay()) return;
    env.step();
  }
  Require(env.context->gameTask->GetMatch()->IsInPlay(),
          label + ": game did not resume");
}

void CheckKinematicMirrorConsistency(GameEnv& env, const std::string& label) {
  Match* match = env.context->gameTask->GetMatch();
  std::vector<Player*> players;
  match->GetActiveTeamPlayers(match->FirstTeam(), players);
  match->GetActiveTeamPlayers(match->SecondTeam(), players);
  std::vector<PlayerBase*> officials;
  match->GetOfficialPlayers(officials);
  Require(!players.empty(), label + ": missing active players");
  Require(!officials.empty(), label + ": missing officials");
  for (const Player* player : players) {
    Require(player->IsKinematicMirrorConsistent(),
            label + ": player kinematic mirror is stale");
  }
  for (const PlayerBase* official : officials) {
    Require(official->IsKinematicMirrorConsistent(),
            label + ": official kinematic mirror is stale");
  }
}

void CheckGoldenSnapshots(GameEnv& env, ScenarioConfig& config) {
  // Values below are a fixed-seed baseline. The hash covers every field in
  // SharedInfo; the explicit values make failures immediately diagnosable.
  struct GoldenSnapshot {
    int calls;
    int step;
    Position ball;
    Position left_player_0;
    Position right_player_0;
    int left_goals;
    int right_goals;
    bool is_in_play;
    uint64_t hash;
  };

  const GoldenSnapshot golden[] = {
      {1, -1, Position(0.0f, 0.0f, 0.110616393f, true),
       Position(-1.01102936f, 0.0f, 0.0f, true),
       Position(1.01102936f, 0.0f, 0.0f, true), 0, 0, false,
       UINT64_C(2178283517849602577)},
      {100, 79, Position(-0.288476169f, 0.287453890f, 0.157757938f, true),
       Position(-0.964185476f, 0.0183162764f, 0.0f, true),
       Position(0.836820960f, -0.000398828211f, 0.0f, true), 0, 0, true,
       UINT64_C(2179468671565013158)},
      {500, 479, Position(0.782679260f, 0.0384375788f, 0.150063261f, true),
       Position(-0.838432312f, 0.00137963647f, 0.0f, true),
       Position(0.986911833f, 0.00512396451f, 0.0f, true), 0, 0, true,
       UINT64_C(2800917500982330357)},
      {1000, 937, Position(0.421206505f, 0.0113820704f, 0.455359012f, true),
       Position(-0.826214671f, -3.12413285e-05f, 0.0f, true),
       Position(0.909096777f, 0.0142614795f, 0.0f, true), 0, 0, true,
       UINT64_C(7544408624481697379)},
  };

  env.reset(config, false);
  int completed = 0;
  for (const GoldenSnapshot& expected : golden) {
    Advance(env, expected.calls - completed);
    completed = expected.calls;
    const SharedInfo info = env.get_info();
    const std::string label = "snapshot at call " + std::to_string(expected.calls);
    Require(info.step == expected.step, label + ": step differs");
    Require(info.left_goals == expected.left_goals &&
                info.right_goals == expected.right_goals,
            label + ": score differs");
    Require(info.is_in_play == expected.is_in_play, label + ": in-play differs");
    Require(!info.left_team.empty() && !info.right_team.empty(),
            label + ": missing active players");
    RequirePositionNear(info.ball_position, expected.ball, label + ": ball");
    RequirePositionNear(info.left_team.front().player_position,
                        expected.left_player_0, label + ": left player 0");
    RequirePositionNear(info.right_team.front().player_position,
                        expected.right_player_0, label + ": right player 0");
    CheckKinematicMirrorConsistency(env, label);
    const uint64_t hash = HashInfo(info);
    if (hash != expected.hash) {
      std::ostringstream message;
      message << label << ": expected hash " << expected.hash << ", got "
              << hash;
      throw RegressionFailure(message.str());
    }
  }
}

void CheckActionExecutorShadows(Match *match, const std::string &label) {
  for (int teamID = 0; teamID < 2; ++teamID) {
    std::vector<Player *> players;
    match->GetTeam(teamID)->GetActivePlayers(players);
    Require(!players.empty(), label + ": missing active players");
    for (const Player *player : players) {
      const PlayerActionState &legacy = player->GetActionState();
      const PlayerActionState &shadow = player->GetActionExecutorShadow();
      Require(legacy.type == shadow.type, label + ": action type differs");
      Require(legacy.frame == shadow.frame, label + ": action frame differs");
      Require(legacy.frameCount == shadow.frameCount,
              label + ": action frame count differs");
      Require(legacy.elapsedTime_ms == shadow.elapsedTime_ms,
              label + ": action elapsed time differs");
      Require(legacy.durationTime_ms == shadow.durationTime_ms,
              label + ": action duration differs");
      Require(legacy.contactTime_ms == shadow.contactTime_ms,
              label + ": contact time differs");
      Require(legacy.contactFrame == shadow.contactFrame,
              label + ": contact frame differs");
      for (int axis = 0; axis < 3; ++axis) {
        RequireNear(shadow.contactPosition.coords[axis],
                    legacy.contactPosition.coords[axis],
                    label + ": contact position");
      }
    }
  }
}

void CheckResetAndStateRoundTrip(GameEnv& env, ScenarioConfig& config) {
  env.reset(config, false);
  CheckKinematicMirrorConsistency(env, "kinematic mirror after reset");
  Advance(env, 300);
  const SharedInfo first_reset = env.get_info();

  env.reset(config, false);
  Advance(env, 300);
  RequireInfoEqual(env.get_info(), first_reset, "repeat reset");
  CheckActionExecutorShadows(env.context->gameTask->GetMatch(),
                             "action executor shadow after reset");
  CheckKinematicMirrorConsistency(env, "kinematic mirror after repeat reset");

  Advance(env, 75);
  const std::string serialized = env.get_state("");
  Advance(env, 125);
  const SharedInfo expected_after_restore = env.get_info();

  env.set_state(serialized);
  Advance(env, 125);
  RequireInfoEqual(env.get_info(), expected_after_restore, "state round-trip");
  CheckActionExecutorShadows(env.context->gameTask->GetMatch(),
                             "action executor shadow after state restore");
  CheckKinematicMirrorConsistency(env, "kinematic mirror after state restore");
}

void CheckMatchTransitions(GameEnv& env, ScenarioConfig& config) {
  env.reset(config, false);
  Require(!env.get_info().is_in_play, "kickoff should begin paused");
  WaitUntilInPlay(env, 30, "initial kickoff");
  Advance(env, 5);  // Let kickoff's set-piece relaxation period elapse.

  Match* match = env.context->gameTask->GetMatch();
  match->GetBall()->SetPosition(
      Vector3(0.0f, pitchHalfH + lineHalfW + 0.5f, 0.5f));
  match->GetBall()->SetMomentum(Vector3(0));
  env.step();
  Require(!match->IsInPlay(), "throw-in should pause play");
  if (match->GetReferee()->GetBuffer().desiredSetPiece != e_GameMode_ThrowIn) {
    std::ostringstream message;
    message << "sideline exit scheduled set piece "
            << match->GetReferee()->GetBuffer().desiredSetPiece;
    throw RegressionFailure(message.str());
  }
  WaitUntilInPlay(env, 60, "throw-in restart");
  Advance(env, 5);  // Let throw-in's set-piece relaxation period elapse.

  match = env.context->gameTask->GetMatch();
  const int score_before = match->GetScore(0) + match->GetScore(1);
  match->GetBall()->SetPosition(
      Vector3(pitchHalfW + lineHalfW - 0.3f, 0.0f, 0.5f));
  match->GetBall()->SetMomentum(Vector3(50.0f, 0.0f, 0.0f));
  env.step();
  Require(match->GetScore(0) + match->GetScore(1) == score_before + 1,
          "crossing the goal line should score");
  Require(!match->IsInPlay(), "goal should pause play for kickoff");
  WaitUntilInPlay(env, 40, "goal kickoff restart");
}

}  // namespace

void PrintBaseline(GameEnv& env, ScenarioConfig& config) {
  const int calls[] = {1, 100, 500, 1000};
  env.reset(config, false);
  int completed = 0;
  std::cout << "  const GoldenSnapshot golden[] = {\n";
  for (const int target : calls) {
    Advance(env, target - completed);
    completed = target;
    const SharedInfo info = env.get_info();
    const Position& ball = info.ball_position;
    const Position& left = info.left_team.front().player_position;
    const Position& right = info.right_team.front().player_position;
    char line[512];
    std::snprintf(line, sizeof(line),
                  "      {%d, %d, Position(%.9gf, %.9gf, %.9gf, true),\n"
                  "       Position(%.9gf, %.9gf, %.9gf, true),\n"
                  "       Position(%.9gf, %.9gf, %.9gf, true), %d, %d, %s,\n"
                  "       UINT64_C(%llu)},\n",
                  target, info.step, ball.env_coord(0), ball.env_coord(1),
                  ball.env_coord(2), left.env_coord(0), left.env_coord(1),
                  left.env_coord(2), right.env_coord(0), right.env_coord(1),
                  right.env_coord(2), info.left_goals, info.right_goals,
                  info.is_in_play ? "true" : "false",
                  static_cast<unsigned long long>(HashInfo(info)));
    std::cout << line;
  }
  std::cout << "  };\n";
}

int main(int argc, char** argv) {
  if (!std::getenv("GFOOTBALL_DATA_DIR")) {
    std::cerr << "Set GFOOTBALL_DATA_DIR before running football_regression.\n";
    return 2;
  }

  try {
    CheckPlayerKinematics();
    CheckPlayerKinematicMirror();
    CheckPlayerGroundCollider();
    CheckPlayerActionExecutor();
    CheckPlayerActionVolume();
    GameEnv env;
    env.game_config.render = false;
    env.start_game();
    ScenarioConfig config = MakeBuiltinAiConfig();

    // Regenerate the golden baseline deliberately and reproducibly instead of
    // hand-editing the snapshot table. Run with --print-baseline and paste the
    // output over the `golden` array in CheckGoldenSnapshots.
    if (argc > 1 && std::string(argv[1]) == "--print-baseline") {
      PrintBaseline(env, config);
      return 0;
    }

    // Officials are placed in the Officials constructor, before any simulation
    // tick runs. Check the mirror here so that a placement path that bypasses
    // the synchronized reset is caught (the first Process would silently heal
    // it later).
    CheckKinematicMirrorConsistency(env, "kinematic mirror after start_game");

    CheckGoldenSnapshots(env, config);
    CheckResetAndStateRoundTrip(env, config);
    CheckMatchTransitions(env, config);
    std::cout << "football_regression: PASS\n";
    return 0;
  } catch (const RegressionFailure& failure) {
    std::cerr << "football_regression: FAIL: " << failure.what() << '\n';
    return 1;
  }
}
