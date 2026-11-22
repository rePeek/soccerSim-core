#include "football/env.hpp"

#include <cstddef>
#include <cstdint>

#include "football/log/log.hpp"

using namespace football::log;

namespace football {

Env::Env(pitch::PitchModel pitch_model, ball::BallModel ball_model,
         std::filesystem::path history_path)
    : pitch_(pitch_model),
      ball_(ball_model),
      history_(history_path.empty() ? history::History()
                                    : history::History::LoadJsonl(history_path)) {
  if (history_.mode() == history::HistoryMode::Recording) {
    ball_.PlaceOnGround(0.0f, 0.0f);
    history_.RecordSnapshot(tick_, ball_.get_state());
  }
  Info(Channel::env, "environment constructed");
}

Env::~Env() { Info(Channel::env, "environment destroyed"); }

void Env::Step() {
  ball_.Step(pitch_);
  tick_ += time::TickSpan{1};
  history_.RecordSnapshot(tick_, ball_.get_state());
}

void Env::LoopMain(time::TickSpan tick_limit) {
  if (history_mode() == history::HistoryMode::Replay) {
    Info(Channel::env, "replay environment does not simulate");
    return;
  }
  for (std::uint64_t advanced = 0; advanced < tick_limit.value; ++advanced) {
    std::lock_guard lock(mutex_);
    try {
      Step();
    } catch (...) {
      Error(Channel::env, "simulation caught an exception and stopped");
      return;
    }
  }
  Info(Channel::env, "simulation reached its tick limit");
}

std::optional<history::Snapshot> Env::ReadSnapshot(time::Tick tick) const {
  std::lock_guard lock(mutex_);
  return history_.ReadSnapshot(tick);
}

history::HistoryMode Env::history_mode() const {
  std::lock_guard lock(mutex_);
  return history_.mode();
}

std::size_t Env::frame_count() const {
  std::lock_guard lock(mutex_);
  return history_.frame_count();
}

time::Tick Env::Ticks() const {
  std::lock_guard lock(mutex_);
  return tick_;
}

}
