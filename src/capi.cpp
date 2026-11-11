#include "football/capi.h"

#include <memory>

#include "football/env.hpp"

namespace {
std::unique_ptr<Env> g_env;
}

extern "C" {

void FOOTBALL_CALL football_init(const football_pitch_model* pitch_model,
                                 const football_ball_model* ball_model) noexcept {
  g_env = std::make_unique<Env>(*pitch_model, *ball_model);
}

void FOOTBALL_CALL football_shutdown(void) noexcept { g_env.reset(); }

}  // extern "C"
