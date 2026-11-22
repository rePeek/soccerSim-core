#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>

#include "football/ball/ball_state.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/history/chunk.hpp"

namespace football::history {

enum class HistoryMode { Recording, Replay };

class History {
 public:
  History();
  History(History&&) = default;
  History& operator=(History&&) = default;
  History(const History&) = delete;
  History& operator=(const History&) = delete;

  void RecordSnapshot(time::Tick tick, const ball::BallState& ball);

  std::optional<Snapshot> ReadSnapshot(time::Tick tick) const;

  void SaveJsonl(const std::filesystem::path& path) const;
  static History LoadJsonl(const std::filesystem::path& path);

  HistoryMode mode() const { return mode_; }

 private:
  struct State {
    Chunk chunk;
    std::mutex mutex;
  };

  explicit History(Chunk chunk);

  HistoryMode mode_;
  std::unique_ptr<State> state_;
};

}
