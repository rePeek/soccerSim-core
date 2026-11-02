#include "import.hpp"

namespace animation_import {
namespace {
constexpr std::array<int, 14> kMirror{0, 1, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 12, 13};
// Same reflection as the source compiler, including metadata not used by poses.
std::string MirrorString(std::string value) {
  if (value.starts_with("left")) return "right" + value.substr(4);
  if (value.starts_with("right")) return "left" + value.substr(5);
  return value;
}
}
ImportedAnimationClip Mirror(const ImportedAnimationClip& source) {
  ImportedAnimationClip clip = source;
  for (std::size_t f = 0; f < clip.poses.size(); ++f) {
    for (std::size_t j = 0; j < kNames.size(); ++j) {
      const auto& q = source.poses[f].orientations[kMirror[j]];
      clip.poses[f].orientations[j].Set(q.elements[0], -q.elements[1], -q.elements[2], q.elements[3]);
      clip.poses[f].positions[j] = source.poses[f].positions[kMirror[j]];
      clip.poses[f].positions[j].coords[0] *= -1;
    }
    clip.root_positions[f].coords[0] *= -1;
  }
  clip.contact_position.coords[0] *= -1;
  for (auto& touch : clip.touches) touch.position.coords[0] *= -1;
  auto& m = clip.metadata;
  m.incoming_body_angle *= -1; m.outgoing_body_angle *= -1; m.outgoing_angle *= -1;
  for (auto* v : {&m.incoming_body_direction, &m.outgoing_direction, &m.incoming_ball_direction,
                  &m.outgoing_ball_direction, &m.bump_direction, &m.incoming_movement,
                  &m.outgoing_movement, &m.translation}) v->coords[0] *= -1;
  m.current_foot = 1 - m.current_foot; m.outgoing_foot = 1 - m.outgoing_foot;
  m.incoming_retain_state = MirrorString(m.incoming_retain_state);
  m.touch_bodypart = MirrorString(m.touch_bodypart);
  constexpr int angle_map[]{0, 6, 7, 8, 9, 5, 1, 2, 3, 4, 10};
  if (m.quadrant > 0 && m.quadrant <= 33)
    m.quadrant = (m.quadrant - 1) / 11 * 11 + angle_map[(m.quadrant - 1) % 11] + 1;
  return clip;
}
}  // namespace animation_import
