#include "import.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace animation_import {
float Yaw(const Quaternion& q) { real x, y, z; q.GetAngles(x, y, z); return z; }
std::vector<MotionTemplate> ReadTemplates(const fs::path& manifest, const fs::path& controls) {
  const auto doc = ReadJson(manifest);
  Check(doc.at("schema") == "football_animation_catalog_v1" && doc.at("fps") == 100,
        "unsupported template catalog");
  Skeleton skeleton(manifest.parent_path() / doc.at("skeleton").get<std::string>());
  const auto control = ReadJson(controls);
  Check(control.at("schema") == "football_motion_generation_keys_v1", "unsupported generation controls");
  std::vector<MotionTemplate> result;
  for (const auto& entry : doc.at("clips")) {
    MotionTemplate t;
    t.clip = ReadGlb(SourcePath(manifest.parent_path(), entry.at("file")), entry, skeleton);
    Check(t.clip.frame_count == 25, "motion templates must span 25 frames");
    for (std::size_t j = 0; j < kNames.size(); ++j) {
      for (const auto& key : control.at("templates").at(t.clip.name).at(std::string(kNames[j])))
        t.keys[j].push_back(Integer(key));
      const auto& keys = t.keys[j];
      Check(keys.size() >= 2 && keys.front() == 0 && keys.back() == 24 &&
            std::adjacent_find(keys.begin(), keys.end(), std::greater_equal<int>()) == keys.end(),
            "invalid generation control samples");
    }
    const auto& root = t.keys[player];
    t.incoming = (t.clip.root_positions[root[1]] - t.clip.root_positions[root[0]]) * (100.0f / (root[1] - root[0]));
    t.outgoing = (t.clip.root_positions[root.back()] - t.clip.root_positions[root[root.size()-2]]) *
                 (100.0f / (root.back() - root[root.size()-2]));
    t.incoming_angle = Yaw(t.clip.poses.front().orientations[body]);
    t.outgoing_angle = t.outgoing.GetLength() >= 1.8f ? Yaw(t.clip.poses.back().orientations[body]) : 0.0f;
    result.push_back(std::move(t));
  }
  Check(!result.empty(), "empty motion templates");
  return result;
}
const MotionTemplate& FindTemplate(const std::vector<MotionTemplate>& templates, float speed, float angle, bool outgoing) {
  const MotionTemplate* best = nullptr;
  float error = 1e10f;
  for (const auto& t : templates) {
    const auto movement = outgoing ? t.outgoing : t.incoming;
    if (FloatToEnumVelocity(movement.GetLength()) != FloatToEnumVelocity(speed)) continue;
    const float difference = std::fabs(float(ModulateIntoRange(-pi, pi,
        (outgoing ? t.outgoing_angle : t.incoming_angle) - angle)));
    if (difference < error) { error = difference; best = &t; }
  }
  Check(best && error < 0.02f, "no matching motion template");
  return *best;
}
ImportedAnimationClip Generate(const Json& entry, const std::vector<MotionTemplate>& templates) {
  const auto metadata = Metadata(entry.at("metadata"));
  const auto& a = FindTemplate(templates, metadata.incoming_velocity, metadata.incoming_body_angle, false);
  const auto& b = FindTemplate(templates, metadata.outgoing_velocity, metadata.outgoing_body_angle, true);
  // The catalog records the output heading, including harmless float roundoff.
  // Recover the generation angle from the finite set used by the motion basis.
  constexpr std::array<float, 9> angles{0, .25f*pi, -.25f*pi, .5f*pi, -.5f*pi,
                                       .75f*pi, -.75f*pi, .99f*pi, -.99f*pi};
  const float angle = *std::min_element(angles.begin(), angles.end(), [&](float x, float y) {
    return std::fabs(x - metadata.outgoing_angle) < std::fabs(y - metadata.outgoing_angle);
  });
  Check(std::fabs(angle - metadata.outgoing_angle) < 0.02f, "unsupported generated heading");
  const float average = std::clamp((GetVelocityID(FloatToEnumVelocity(metadata.incoming_velocity)) +
                                   GetVelocityID(FloatToEnumVelocity(metadata.outgoing_velocity))) / 6.0f, 0.0f, 1.0f);
  ImportedAnimationClip clip;
  clip.poses.resize(25);
  clip.root_positions.resize(25);
  for (std::size_t joint = 0; joint < kNames.size(); ++joint) {
    std::set<int> keys(a.keys[joint].begin(), a.keys[joint].end());
    keys.insert(b.keys[joint].begin(), b.keys[joint].end());
    if (joint == player) { keys.insert(1); keys.insert(23); for (int f = 2; f < 23; f += 4) keys.insert(f); }
    std::vector<int> frames(keys.begin(), keys.end());
    std::vector<Quaternion> rotations;
    std::vector<Vector3> positions;
    Vector3 cumulative(0);
    const auto outgoing = b.outgoing.GetRotated2D(angle);
    auto change = (outgoing - a.incoming) * 4.0f;
    int previous = 0;
    for (int frame : frames) {
      const float original = std::clamp(frame - 1.0f, 0.0f, 22.0f) / 22.0f;
      const float bias = curve(std::pow(original, .3f + .4f * average +
          .3f * std::clamp(change.GetLength() / 20.0f, 0.0f, 1.0f)), .7f);
      auto q = a.clip.poses[frame].orientations[joint].GetSlerped(bias, b.clip.poses[frame].orientations[joint]);
      if (joint == body) {
        Quaternion turn; turn.SetAngleAxis(angle * (bias * .7f + original * .3f), Vector3(0,0,1));
        q = turn * q;
        // Keep the original generator's per-control-sample leaning evolution.
        change *= .5f + .5f * ((a.incoming * (1-bias) + outgoing * bias).GetLength() / sprintVelocity);
        Quaternion lean;
        lean.SetAngleAxis(change.GetLength() * .001f * (.5f + .5f * std::sin(original*pi)),
                         Vector3(0,1,0).GetRotated2D(change.GetNormalized(0).GetAngle2D()));
        q = lean * q;
      }
      Vector3 position(0);
      if (joint == player) {
        cumulative += a.incoming * ((frame-previous)*.01f) * (1-bias) + outgoing * ((frame-previous)*.01f) * bias;
        position = cumulative;
        position.coords[2] = a.clip.poses[frame].positions[player].coords[2] * (1-bias) +
                             b.clip.poses[frame].positions[player].coords[2] * bias;
      }
      rotations.push_back(q); positions.push_back(position); previous = frame;
    }
    for (int f = 0; f < 25; ++f) {
      auto hi = std::lower_bound(frames.begin(), frames.end(), f) - frames.begin();
      auto lo = hi > 0 ? hi-1 : hi;
      const float t = hi == lo ? 0 : float(f-frames[lo]) / (frames[hi]-frames[lo]);
      clip.poses[f].orientations[joint] = f == frames[hi] ? rotations[hi] : rotations[lo].GetSlerped(t, rotations[hi]);
      clip.poses[f].positions[joint] = positions[lo] * (1-t) + positions[hi] * t;
    }
  }
  for (int f = 0; f < 25; ++f) clip.root_positions[f] = clip.poses[f].positions[player].Get2D();
  ApplyEntry(clip, entry);
  return clip;
}
}  // namespace animation_import
