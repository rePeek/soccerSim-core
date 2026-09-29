// Headless football simulation: drives the core layer (state + physics +
// contact) directly, with no Match / Humanoid / animation / graphics.
//
// This is deliberately small on purpose: it proves the core simulation can
// advance a WorldState on its own and print a deterministic trajectory.

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "core/contact/player_contact.hpp"
#include "core/domain/ball/ball_profile.hpp"
#include "core/domain/player/player_profile.hpp"
#include "core/physics/ball_physics.hpp"
#include "core/state/world_state.hpp"

namespace {

constexpr float kDt = 0.01f;  // 10 ms tick, matches the engine cadence

}  // namespace

int main(int argc, char **argv) {
  const int ticks = argc > 1 ? std::atoi(argv[1]) : 300;

  football::domain::BallProfile ball_profile;
  football::domain::PlayerProfile player_profile;

  WorldState world;
  world.ball.position = blunted::Vector3(0.0f, 0.0f, 0.5f);
  world.ball.velocity = blunted::Vector3(4.0f, 2.0f, 3.0f);

  // Two simple players moving toward each other.
  world.players[0].position = blunted::Vector3(5.0f, 0.0f, 0.0f);
  world.players[0].velocity = blunted::Vector3(-2.0f, 0.0f, 0.0f);
  world.players[1].position = blunted::Vector3(-5.0f, 0.0f, 0.0f);
  world.players[1].velocity = blunted::Vector3(2.0f, 0.0f, 0.0f);

  GoalGeometry goal;
  std::printf("headless footballSim: %d ticks, dt=%.2f\n", ticks, kDt);

  for (int tick = 0; tick < ticks; ++tick) {
    // Ball: free flight + ground + woodwork (no moving players yet).
    world.ball =
        BallPhysics::Step(world.ball, kDt, ball_profile, true, goal, {}).state;

    // Trivial player motion for the demo.
    for (auto &player : world.players) {
      player.position += player.velocity * kDt;
    }

    // Player-player ground contact, deterministic by index.
    std::vector<football::contact::PlayerContactBody> bodies;
    for (int i = 0; i < 2; ++i) {
      football::contact::PlayerContactBody body;
      body.index = i;
      body.profile = player_profile;
      body.state = world.players[i];
      bodies.push_back(body);
    }
    football::contact::ResolvePlayerContactBatch(bodies);
    for (int i = 0; i < 2; ++i) {
      world.players[i] = bodies[i].state;
    }

    if (tick % 10 == 0) {
      const blunted::Vector3 &p = world.ball.position;
      const blunted::Vector3 &v = world.ball.velocity;
      std::printf("t=%6.3f  ball=(%7.2f, %7.2f, %6.2f)  v=(%6.2f, %6.2f, %6.2f)\n",
                  static_cast<double>((tick + 1) * kDt),
                  static_cast<double>(p.coords[0]),
                  static_cast<double>(p.coords[1]),
                  static_cast<double>(p.coords[2]),
                  static_cast<double>(v.coords[0]),
                  static_cast<double>(v.coords[1]),
                  static_cast<double>(v.coords[2]));
    }
  }
  return 0;
}