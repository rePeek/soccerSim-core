#include "football/capi.h"

#include <cstddef>
#include <cstdio>
#include <exception>
#include <memory>
#include <new>
#include <type_traits>

#include "football/env.hpp"

static_assert(sizeof(float) == 4);
static_assert(std::is_standard_layout_v<football_pitch_model>);
static_assert(std::is_standard_layout_v<football_ball_model>);
static_assert(sizeof(football_pitch_model) == 20);
static_assert(sizeof(football_ball_model) == 12);
static_assert(offsetof(football_pitch_model, air_drag) == 16);
static_assert(offsetof(football_ball_model, magnus_coefficient) == 8);
static_assert(std::is_same_v<football::pitch::PitchModel, football_pitch_model>);
static_assert(std::is_same_v<football::ball::BallModel, football_ball_model>);

namespace {
thread_local char last_error[512] = {};
std::unique_ptr<Env> g_env;

football_status Fail(football_status status, const char* message) noexcept {
  // Fixed storage: reporting even an allocation failure must not allocate.
  std::snprintf(last_error, sizeof(last_error), "%s", message);
  return status;
}
}  // namespace

extern "C" {

football_status FOOTBALL_CALL football_init(
    const football_pitch_model* pitch_model,
    const football_ball_model* ball_model) noexcept {
  last_error[0] = '\0';
  if (!pitch_model || !ball_model) {
    return Fail(FOOTBALL_INVALID_ARGUMENT, "model pointer is NULL");
  }
  if (g_env) return Fail(FOOTBALL_INVALID_ARGUMENT, "already initialized");
  try {
    g_env = std::make_unique<Env>(*pitch_model, *ball_model);
    return FOOTBALL_OK;
  } catch (const std::bad_alloc&) {
    return Fail(FOOTBALL_OUT_OF_MEMORY, "out of memory");
  } catch (const std::exception& e) {
    return Fail(FOOTBALL_INTERNAL_ERROR, e.what());
  } catch (...) {
    return Fail(FOOTBALL_INTERNAL_ERROR, "unknown C++ exception");
  }
}

void FOOTBALL_CALL football_shutdown(void) noexcept { g_env.reset(); }

const char* FOOTBALL_CALL football_last_error(void) noexcept { return last_error; }

}  // extern "C"
