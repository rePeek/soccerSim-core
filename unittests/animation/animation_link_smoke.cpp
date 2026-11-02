#include "football/animation/library.hpp"
#include "football/animation/pose_blend.hpp"
#include "football/animation/reference_sample.hpp"

// No src/ include path, Player, world actors, catalog load or external state.
int main() {
  AnimationLibrary library;
  if (library.Size() != 0) return 1;
  AnimationClip clip;
  clip.frame_count = 2;
  clip.root_positions = {Vector3(0), Vector3(0, -0.1f, 0)};
  clip.poses.resize(2);
  const auto sample = SampleAnimationStep(clip, 0.0f, 1.0f);
  if (sample.root_delta != Vector3(0, -0.1f, 0)) return 2;
  const auto pose = BlendPose(clip.poses.front(), sample.pose, 1.0f);
  return pose.positions[BodyPart::player] == sample.pose.positions[BodyPart::player]
             ? 0 : 3;
}
