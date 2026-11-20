#include "football/capi.h"

#include <new>

#include "football/env.hpp"
#include "football/log/log.hpp"
#include "football/runner.hpp"

using namespace football::log;

// The opaque handle is bound to one runner, which owns one match and one worker.
// Nothing is published or duplicated: the progress queries read the match the
// runner owns, which is safe once the caller has paused, because Pause() returns
// only after the worker is parked.
struct football_session {
  football_session(const football_pitch_model& pitch_model,
                   const football_ball_model& ball_model)
      : runner(pitch_model, ball_model) {}

  football::runner::SingleRunner runner;
};

extern "C" {

football_session* FOOTBALL_CALL football_session_create(
    const football_pitch_model* pitch_model,
    const football_ball_model* ball_model) noexcept {
  if (pitch_model == nullptr || ball_model == nullptr) {
    Warning(Channel::capi, "football_session_create ignored a NULL model");
    return nullptr;
  }

  try {
    auto* handle = new football_session(*pitch_model, *ball_model);
    Info(Channel::capi, "football_session_create completed");
    return handle;
  } catch (...) {
    // A failed worker start must leave no half-built handle behind.
    Error(Channel::capi, "football_session_create failed");
    return nullptr;
  }
}

void FOOTBALL_CALL football_session_destroy(football_session* session) noexcept {
  if (session != nullptr) {
    Info(Channel::capi, "football_session_destroy released a session");
    delete session;  // stops and joins the runner before releasing the match
  }
  // Teardown also publishes the buffered log, so a host never flushes by hand.
  Flush();
}

void FOOTBALL_CALL football_resume(football_session* session) noexcept {
  if (session) session->runner.Resume();
}

void FOOTBALL_CALL football_pause(football_session* session) noexcept {
  if (session) session->runner.Pause();
}

int FOOTBALL_CALL football_running(const football_session* session) noexcept {
  return session && session->runner.IsRunning() ? 1 : 0;
}
}  // extern "C"
