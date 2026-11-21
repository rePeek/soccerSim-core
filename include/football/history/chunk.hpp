#pragma once

#include <vector>

#include "football/history/snapshot.hpp"

namespace football::history {

// One contiguous run of frames. Compression, indexing and streaming would split
// a match into several chunks; today every match records exactly one.
struct Chunk {
  std::vector<Snapshot> frames;
};

}  // namespace football::history
