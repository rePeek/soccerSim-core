#pragma once

#include <filesystem>
#include <mutex>
#include <optional>

#include "football/ball/ball.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/history/history.hpp"
#include "football/pitch/pitch.hpp"

namespace football {

inline constexpr time::TickSpan kDefaultRunTicks{50000};

class Env {
 public:
  explicit Env(pitch::PitchModel pitch_model, ball::BallModel ball_model,
               std::filesystem::path history_path = {});
  ~Env();

  Env(const Env&) = delete;
  Env& operator=(const Env&) = delete;
  Env(Env&&) = delete;
  Env& operator=(Env&&) = delete;

  void LoopMain(time::TickSpan tick_limit = kDefaultRunTicks);

  std::optional<history::Snapshot> ReadSnapshot(time::Tick tick) const;
  history::HistoryMode history_mode() const;
  std::size_t frame_count() const;

  time::Tick Ticks() const;

 private:
  void Step();

  pitch::Pitch pitch_;
  ball::Ball ball_;
  history::History history_;
  time::Tick tick_{0};
  mutable std::mutex mutex_;
};

}
