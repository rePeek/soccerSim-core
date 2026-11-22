#pragma once

#include <vector>

#include "football/history/snapshot.hpp"

namespace football::history {

struct Chunk {
  std::vector<Snapshot> frames;
};

}
