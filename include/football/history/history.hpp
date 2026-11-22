#pragma once

#include <filesystem>
#include <optional>

#include "football/ball/ball_state.hpp"
#include "football/foundation/time/tick.hpp"
#include "football/history/chunk.hpp"

namespace football::history {

enum class HistoryMode { Recording, Replay };

class History {
 public:
  History();

  void RecordSnapshot(time::Tick tick, const ball::BallState& ball);

  std::optional<Snapshot> ReadSnapshot(time::Tick tick) const;

  std::size_t frame_count() const { return chunk_.frames.size(); }

  void SaveJsonl(const std::filesystem::path& path) const;
  static History LoadJsonl(const std::filesystem::path& path);

  HistoryMode mode() const { return mode_; }

 private:
  explicit History(Chunk chunk);

  Chunk chunk_;
  HistoryMode mode_;
};

}
