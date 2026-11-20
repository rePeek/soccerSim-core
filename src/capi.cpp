#include "football/capi.h"

#include <new>

#include "football/env.hpp"
#include "football/log/log.hpp"
#include "runner/runner.hpp"

// The C ABI documents the phase as an integer, so pin the mapping it promises.
static_assert(static_cast<int>(MatchPhase::FirstHalf) == 0, "ABI: first half");
static_assert(static_cast<int>(MatchPhase::HalfTime) == 1, "ABI: half time");
static_assert(static_cast<int>(MatchPhase::SecondHalf) == 2, "ABI: second half");
static_assert(static_cast<int>(MatchPhase::FullTime) == 3, "ABI: full time");

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
  if (session == nullptr) return;
  Info(Channel::capi, "football_session_destroy released a session");
  delete session;  // stops and joins the runner before releasing the match
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

/* Progress reads the match the runner owns, so call football_pause() first (it
 * returns once the worker is parked) or read it after the loop has ended. */
int FOOTBALL_CALL football_finished(const football_session* session) noexcept {
  return session && session->runner.Environment().Finished() ? 1 : 0;
}

uint64_t FOOTBALL_CALL football_ticks(const football_session* session) noexcept {
  return session ? session->runner.Environment().Ticks().value : 0;
}

int FOOTBALL_CALL football_phase(const football_session* session) noexcept {
  return session ? static_cast<int>(session->runner.Environment().Phase()) : 0;
}

void FOOTBALL_CALL football_flush(void) noexcept { Flush(); }

}  // extern "C"
