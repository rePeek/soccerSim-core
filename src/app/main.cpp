#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/model/world.hpp"
#include "core/physics/physics_system.hpp"

namespace {

constexpr float kTickSeconds = 0.01f;

football::model::Ball ParseBall(const nlohmann::json& config) {
  return football::model::Ball(
      config.value("radius", 0.11f), config.value("mass", 0.43f),
      config.value("inertiaFactor", 2.0f / 3.0f));
}

std::vector<football::model::Player> ParsePlayers(
    const nlohmann::json& config) {
  std::vector<football::model::Player> players;
  if (!config.contains("players")) return players;

  const nlohmann::json& entries = config.at("players");
  if (!entries.is_array()) {
    throw std::runtime_error("'players' must be an array");
  }
  players.reserve(entries.size());
  for (const nlohmann::json& entry : entries) {
    players.emplace_back(
        entry.at("id").get<football::model::PlayerId>(),
        entry.value("height", 1.80f), entry.value("mass", 75.0f),
        entry.value("bodyRadius", 0.36f), entry.value("strength", 0.5f),
        entry.value("balance", 0.5f));
  }
  return players;
}

int ParseTicks(const char* text) {
  const int ticks = std::atoi(text);
  if (ticks <= 0) throw std::runtime_error("tick count must be positive");
  return ticks;
}

bool IsTickArgument(const char* text) {
  if (*text == '\0') return false;
  for (const char* cursor = text; *cursor != '\0'; ++cursor) {
    if (*cursor < '0' || *cursor > '9') return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  std::string configPath = "config/headless.json";
  int ticks = 300;

  try {
    if (argc > 1) {
      if (IsTickArgument(argv[1])) {
        ticks = ParseTicks(argv[1]);
      } else {
        configPath = argv[1];
        if (argc > 2) ticks = ParseTicks(argv[2]);
      }
    }

    std::ifstream input(configPath);
    if (!input) {
      throw std::runtime_error("cannot open profile JSON: " + configPath);
    }
    const nlohmann::json config = nlohmann::json::parse(input);
    if (!config.contains("ball") || !config.at("ball").is_object()) {
      throw std::runtime_error("profile JSON requires a 'ball' object");
    }

    football::model::World world(ParseBall(config.at("ball")),
                                 ParsePlayers(config));
    PhysicsSystem physics;
    std::printf("footballSim: profile=%s, players=%zu, ticks=%d, dt=%.2f\n",
                configPath.c_str(), world.Players().size(), ticks,
                static_cast<double>(kTickSeconds));
    for (int tick = 0; tick < ticks; ++tick) {
      physics.Step(world, kTickSeconds);
      if (tick % 10 == 0) {
        const football::model::BallState& state = world.GetBall().State();
        std::printf(
            "t=%6.3f ball=(%7.2f, %7.2f, %6.2f) v=(%6.2f, %6.2f, %6.2f)\n",
            static_cast<double>((tick + 1) * kTickSeconds),
            static_cast<double>(state.position.coords[0]),
            static_cast<double>(state.position.coords[1]),
            static_cast<double>(state.position.coords[2]),
            static_cast<double>(state.velocity.coords[0]),
            static_cast<double>(state.velocity.coords[1]),
            static_cast<double>(state.velocity.coords[2]));
      }
    }
  } catch (const std::exception& error) {
    std::fprintf(stderr, "footballSim: %s\n", error.what());
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
