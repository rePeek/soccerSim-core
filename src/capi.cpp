#include "football/capi.h"

#include <memory>

#include "football/env.hpp"
#include "football/log/log.hpp"

using namespace football::log;

namespace {
std::unique_ptr<Env> g_env;
}

extern "C" {

void FOOTBALL_CALL football_init(const football_pitch_model* pitch_model,
                                 const football_ball_model* ball_model) noexcept {
  if (g_env) Info(Channel::capi, "football_init replaced the live environment");
  g_env = std::make_unique<Env>(*pitch_model, *ball_model);
  Info(Channel::capi, "football_init completed");
}

void FOOTBALL_CALL football_shutdown(void) noexcept {
  if (!g_env) {
    Debug(Channel::capi, "football_shutdown called with no environment");
  } else {
    Info(Channel::capi, "football_shutdown released the environment");
  }
  g_env.reset();
  Flush();
}

}  // extern "C"
