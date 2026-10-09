#include "env.hpp"

#include <stdexcept>
#include <utility>
#include <bq_log/bq_log.h>

#include "ai/default_ai.hpp"
#include "sim/simulation.hpp"

namespace {

void InitLogging() {
  const auto logger = bq::log::create_log("football", R"(
    appenders_config.console.type=console
    appenders_config.console.levels=[warning,error,fatal]
    log.thread_mode=async
    log.buffer_size=65536
    log.reliable_level=normal
  )");
  if (!logger.is_valid()) {
    throw std::runtime_error("Unable to initialize football logger");
  }
}

void FlushLogging() {
  auto logger = bq::log::get_log_by_name("football");
  if (logger.is_valid()) logger.force_flush();
}

}  // namespace

GameEnv::GameEnv(football::model::Team home, football::model::Team away,
                 football::model::Pitch pitch, football::model::BallConfig ball_config,
                 MatchOptions match_options,
                 football::ai::AIConfig ai_config)
    : home_team_(std::move(home)), away_team_(std::move(away)),
      pitch_(std::move(pitch)), ball_config_(std::move(ball_config)),
      match_options_(match_options),
      ai_config_(std::move(ai_config)) {}

GameEnv::~GameEnv() { Stop(); }

void GameEnv::Start() {
  if (simulation_) throw std::logic_error("match runner already started");
  InitLogging();
  try {
    auto simulation = std::make_unique<Simulation>();
    simulation->Init(home_team_, away_team_, pitch_, match_options_, ball_config_);
    auto ai = std::make_unique<football::ai::DefaultAI>(
        home_team_, away_team_, pitch_, ai_config_);
    simulation_ = std::move(simulation);
    ai_ = std::move(ai);
  } catch (...) {
    FlushLogging();
    throw;
  }
}

void GameEnv::Step() {
  if (!simulation_) throw std::logic_error("match runner is stopped");
  if (simulation_->Finished()) return;
  const WorldState world = simulation_->Observe();
  PlayerControlSet controls;
  ai_->Update(world, controls);
  simulation_->Step(controls);
}

bool GameEnv::Finished() const {
  return simulation_ && simulation_->Finished();
}

MatchResult GameEnv::Result() const {
  if (!simulation_) throw std::logic_error("match runner has no final result");
  return simulation_->Result();
}

WorldState GameEnv::Observe() const {
  if (!simulation_) throw std::logic_error("match runner is stopped");
  return simulation_->Observe();
}

void GameEnv::Stop() {
  ai_.reset();
  simulation_.reset();
  FlushLogging();
}
