// Copyright 2019 Google LLC & Bastiaan Konings
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Standalone animation baker. It drives the legacy AnimCollection::Load()/
// _PrepareAnim() importer directly (no runtime environment) and serializes the
// prepared clips into a plain binary "simanim" artifact.

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>

#include "animation/animcollection.hpp"
#include "animation/animation.hpp"
#include "animation/clip.hpp"
#include "animation/library.hpp"
#include "animation/extensions/footballanimationextension.hpp"
#include "foundation/math/vector3.hpp"

using namespace blunted;

namespace {

std::vector<AnimationClip> BakeClips(const std::vector<Animation*>& animations) {
  std::vector<AnimationClip> clips;
  clips.reserve(animations.size());
  for (size_t i = 0; i < animations.size(); ++i) {
    Animation* anim = animations[i];
    AnimationClip clip;
    clip.id = static_cast<uint32_t>(i);
    clip.name = anim->GetName();
    clip.frame_count = static_cast<uint32_t>(anim->GetFrameCount());
    clip.incoming_velocity = anim->GetIncomingVelocity();
    clip.outgoing_velocity = anim->GetOutgoingVelocity();
    clip.incoming_body_angle = anim->GetIncomingBodyAngle();
    clip.outgoing_body_angle = anim->GetOutgoingBodyAngle();
    clip.anim_difficulty =
        static_cast<float>(std::atof(anim->GetVariable("animdifficultyfactor").c_str()));
    clip.touch_frame = std::atoi(anim->GetVariable("touchframe").c_str());
    clip.quadrant_id = std::atoi(anim->GetVariable("quadrant_id").c_str());
    clip.anim_type = static_cast<int32_t>(anim->GetAnimType());

    // Contact (first touch).
    if (clip.touch_frame >= 0) {
      Vector3 contact_pos;
      if (std::static_pointer_cast<FootballAnimationExtension>(
              anim->GetExtension("football"))
              ->GetTouchPos(clip.touch_frame, contact_pos)) {
        clip.has_contact = true;
        clip.contact_position = contact_pos;
      }
    }

    clip.root_positions.reserve(clip.frame_count);
    clip.poses.reserve(clip.frame_count);
    for (uint32_t frame = 0; frame < clip.frame_count; ++frame) {
      // Root motion (player body part, z zeroed for 2D simulation).
      Quaternion orientation;
      Vector3 position;
      anim->GetKeyFrame(player, static_cast<int>(frame), orientation, position);
      position.coords[2] = 0.0f;
      clip.root_positions.push_back(position);

      // Full per-joint pose.
      PoseFrame pose;
      for (size_t b = 0; b < kBodyPartCount; ++b) {
        Quaternion o;
        Vector3 p;
        anim->GetKeyFrame(static_cast<BodyPart>(b), static_cast<int>(frame), o, p);
        pose.orientations[b] = o;
        pose.positions[b] = p;
      }
      clip.poses.push_back(std::move(pose));
    }

    clips.push_back(std::move(clip));
  }
  return clips;
}

void Serialize(std::ostream& os, const std::vector<AnimationClip>& clips) {
  SimAnimWriteHeader(os, static_cast<uint32_t>(clips.size()));
  for (const AnimationClip& clip : clips) {
    clip.Serialize(os);
  }
}

std::vector<char> SerializeToBuffer(const std::vector<AnimationClip>& clips) {
  std::ostringstream oss(std::ios::binary);
  Serialize(oss, clips);
  const std::string s = oss.str();
  return std::vector<char>(s.begin(), s.end());
}

int Export(const std::string& path, const std::vector<AnimationClip>& clips) {
  std::ofstream os(path, std::ios::binary | std::ios::trunc);
  if (!os) {
    std::cerr << "anim_baker: cannot open " << path << " for writing\n";
    return 1;
  }
  Serialize(os, clips);
  std::cout << "anim_baker: wrote " << clips.size() << " clips to " << path
            << "\n";
  return 0;
}

int Check(const std::string& path, const std::vector<AnimationClip>& baked) {
  AnimationLibrary library;
  if (!library.Load(path)) {
    std::cerr << "anim_baker: CHECK FAILED: cannot load " << path << "\n";
    return 1;
  }
  if (library.Size() != baked.size()) {
    std::cerr << "anim_baker: CHECK FAILED: clip count mismatch\n";
    return 1;
  }

  // Re-derive the expected bytes from the freshly baked clips and compare.
  const std::vector<char> expected = SerializeToBuffer(baked);
  std::ifstream is(path, std::ios::binary);
  is.seekg(0, std::ios::end);
  const std::streamoff size = is.tellg();
  is.seekg(0, std::ios::beg);
  std::vector<char> on_disk(static_cast<size_t>(size));
  is.read(on_disk.data(), size);

  if (on_disk.size() != expected.size() ||
      std::memcmp(on_disk.data(), expected.data(), expected.size()) != 0) {
    std::cerr << "anim_baker: CHECK FAILED: artifact does not match a fresh bake\n";
    return 1;
  }
  std::cout << "anim_baker: CHECK PASSED (" << baked.size() << " clips, "
            << expected.size() << " bytes, byte-identical + readable)\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  std::string input_dir;
  std::string out_path;
  std::string check_path;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--input" && i + 1 < argc) {
      input_dir = argv[++i];
    } else if (arg == "--out" && i + 1 < argc) {
      out_path = argv[++i];
    } else if (arg == "--check" && i + 1 < argc) {
      check_path = argv[++i];
    } else {
      std::cerr << "usage: " << argv[0] << " --input DIR [--out FILE] [--check FILE]\n";
      return 2;
    }
  }

  if (input_dir.empty()) {
    std::cerr << "anim_baker: --input DIR is required\n";
    return 2;
  }
  if (out_path.empty() && check_path.empty()) {
    std::cerr << "anim_baker: specify --out FILE and/or --check FILE\n";
    return 2;
  }

  const std::filesystem::path data_dir = std::filesystem::absolute(input_dir);
  AnimationSourcePaths paths{
      (data_dir / "media/animations").string(),
      (data_dir / "media/animations/templates").string(),
      (data_dir / "media/objects/players/player.object").string(),
  };

  AnimCollection anims;
  anims.Load(paths);

  const std::vector<AnimationClip> clips = BakeClips(anims.GetAnimations());

  int status = 0;
  if (!out_path.empty()) status |= Export(out_path, clips);
  if (!check_path.empty()) status |= Check(check_path, clips);
  return status;
}