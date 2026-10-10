#include "sim/observation/touch_inference.hpp"

#include <algorithm>
#include <cstdint>

#include "foundation/math/scalar.hpp"

namespace football::sim::observation {
namespace {

// Heuristic bounds. These are ordering aids, not calibrated football values.
constexpr float kMaxTouchDistance = 3.0f;
constexpr float kCloseTouchDistance = 1.2f;
constexpr float kDistanceFalloff = 4.0f;
constexpr float kTouchFrameBase = 0.55f;
constexpr float kBallspeedForFullEvidence = 6.0f;
constexpr float kBallMoveForFullEvidence = 1.5f;

// A touch frame is evidence only while the same animation is still running.
// Frames strictly after the previous sample and at or before the current
// sample count as crossed. A changed animation id is not inferable: the solver
// may have switched clips right after the touch.
bool CrossedTouchFrame(const PlayerSnapshot& previous, const PlayerSnapshot& current,
                       const AnimationClip& clip) {
  if (previous.animation_id != current.animation_id) return false;
  const auto crossed = [&](int frame) {
    return frame >= 0 && previous.frame < static_cast<std::uint32_t>(frame) &&
           static_cast<std::uint32_t>(frame) <= current.frame;
  };
  if (crossed(clip.metadata.touch_frame)) return true;
  for (const BakedTouch& touch : clip.touches) {
    if (crossed(touch.frame)) return true;
  }
  return false;
}

}  // namespace

std::vector<TouchCandidate> InferTouches(const SnapshotRecord& previous,
                                         const SnapshotRecord& current,
                                         const SnapshotMetadata& metadata,
                                         const AnimationLibrary& animations) {
  std::vector<TouchCandidate> candidates;
  // Refuse every boundary the snapshots cannot describe.
  if (previous.stamp.generation != current.stamp.generation) return candidates;
  if (current.stamp.step_index != previous.stamp.step_index + 1) return candidates;
  const std::size_t count = current.snapshot.players.size();
  if (previous.snapshot.players.size() != count) return candidates;
  if (metadata.player_ids.size() != count) return candidates;

  const BallSnapshot& previous_ball = previous.snapshot.ball;
  const BallSnapshot& current_ball = current.snapshot.ball;
  const float ball_speed_delta = (current_ball.velocity - previous_ball.velocity).GetLength();
  const float ball_displacement = (current_ball.position - previous_ball.position).GetLength();

  for (std::size_t i = 0; i < count; ++i) {
    const PlayerSnapshot& prev = previous.snapshot.players[i];
    const PlayerSnapshot& cur = current.snapshot.players[i];
    if (!prev.active || !cur.active) continue;
    const AnimationId id = cur.animation_id;
    if (id < 0 || static_cast<std::size_t>(id) >= animations.Size()) continue;
    const AnimationClip& clip = animations.Get(static_cast<std::uint32_t>(id));
    if (!CrossedTouchFrame(prev, cur, clip)) continue;

    // Spatial gate: a scheduled touch frame with the ball out of reach is a
    // fake/no-contact animation, not a touch.
    const float proximity = (current_ball.position - cur.position).GetLength();
    if (proximity > kMaxTouchDistance) continue;

    float confidence = kTouchFrameBase;
    confidence += NormalizedClamp(ball_speed_delta, 0.0f, kBallspeedForFullEvidence) * 0.25f;
    confidence += NormalizedClamp(ball_displacement, 0.0f, kBallMoveForFullEvidence) * 0.2f;
    confidence -= NormalizedClamp(proximity - kCloseTouchDistance, 0.0f, kDistanceFalloff) * 0.5f;
    confidence = clamp(confidence, 0.0f, 1.0f);
    if (confidence <= 0.0f) continue;
    candidates.push_back(TouchCandidate{metadata.player_ids[i], confidence});
  }

  // Stable sort keeps the roster slot order as a deterministic tie-break.
  std::stable_sort(candidates.begin(), candidates.end(),
                   [](const TouchCandidate& a, const TouchCandidate& b) {
                     return a.confidence > b.confidence;
                   });
  return candidates;
}

}  // namespace football::sim::observation
