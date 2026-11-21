#include "football/capi.h"

#include <cstdint>
#include <new>

#include "football/log/log.hpp"
#include "football/runner.hpp"

using namespace football::log;

struct football_session {
  football_session(const football_pitch_model& pitch_model,
                   const football_ball_model& ball_model,
                   std::uint64_t ticks_per_step)
      : runner(pitch_model, ball_model, football::history::Recording::Enabled,
               football::time::TickSpan{ticks_per_step}) {}

  football::runner::SingleRunner runner;
};

extern "C" {

football_session* FOOTBALL_CALL football_session_create(
    const football_pitch_model* pitch_model,
    const football_ball_model* ball_model,
    uint64_t ticks_per_step) noexcept {
  if (pitch_model == nullptr || ball_model == nullptr) {
    Warning(Channel::capi, "football_session_create ignored a NULL model");
    return nullptr;
  }

  try {
    auto* handle =
        new football_session(*pitch_model, *ball_model, ticks_per_step);
    Info(Channel::capi, "football_session_create completed");
    return handle;
  } catch (...) {
    Error(Channel::capi, "football_session_create failed");
    return nullptr;
  }
}

void FOOTBALL_CALL football_session_start(football_session* session) noexcept {
  if (session) session->runner.Start();
}

void FOOTBALL_CALL football_session_destroy(football_session* session) noexcept {
  if (session != nullptr) {
    Info(Channel::capi, "football_session_destroy released a session");
    delete session;
  }
  Flush();
}

int FOOTBALL_CALL football_get_render_snapshot(
    const football_session* session, uint64_t tick,
    football_render_snapshot* output) noexcept {
  if (session == nullptr || output == nullptr) return 0;

  try {
    const auto snapshot = session->runner.history().ReadSnapshot(
        football::time::Tick{tick});
    if (!snapshot) return 0;

    const auto& position = snapshot->ball.position.coords;
    const auto& orientation = snapshot->ball.orientation.elements;
    *output = football_render_snapshot{
        snapshot->tick.value, position[0], position[1], position[2],
        orientation[0], orientation[1], orientation[2], orientation[3]};
    return 1;
  } catch (...) {
    return 0;
  }
}

}
