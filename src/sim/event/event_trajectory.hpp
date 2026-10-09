#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "foundation/math/vector3.hpp"
#include "sim/event/event.hpp"

namespace football::sim::event {

enum class TrajectoryKind { Pass, Shot };

// Additive whole-step motion analysis, never contact evidence or a ruling.
// Samples span the executed steps containing action start through resolution,
// including the resolution step's final committed state. Missing retained data,
// reset discontinuities and time/step gaps mark the window incomplete; distance
// is accumulated only between consecutive samples within the same generation.
struct EventTrajectory {
  EventId event = kInvalidEventId;
  TrajectoryKind kind = TrajectoryKind::Pass;
  model::PlayerId actor = model::kInvalidPlayerId;
  model::TeamSide team = model::TeamSide::Home;
  std::uint64_t generation = 0;
  std::uint64_t first_step = 0;
  std::uint64_t last_step = 0;
  std::size_t sample_count = 0;
  std::optional<blunted::Vector3> first_ball_position;
  std::optional<blunted::Vector3> last_ball_position;
  float ball_path_length = 0;
  float peak_ball_speed = 0;
  bool complete = false;
};

}  // namespace football::sim::event
