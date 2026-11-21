#include "football/capi.h"

#include <new>

#include "football/log/log.hpp"
#include "football/runner.hpp"

using namespace football::log;

// The opaque handle is bound to one runner, which owns one match and one worker:
// creating a session starts that loop and destroying it stops and joins the loop.
// The match records its frames in-process; nothing delivers them over the ABI yet.
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

}  // extern "C"
