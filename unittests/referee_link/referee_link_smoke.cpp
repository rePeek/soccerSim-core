// Independent Referee boundary smoke test.
//
// The executable includes only the public include tree and force-links every
// football_referee object through a WHOLE_ARCHIVE link. The value-only rule
// library must close over foundation + pitch alone; any Referee -> Player,
// Event, Runtime or AI undefined symbol makes this fail to link.
#include "football/pitch/pitch.hpp"

int main() {
  const football::pitch::Pitch pitch;
  return pitch.half_length() > 0.0f ? 0 : 1;
}
