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
    clip.metadata.incoming_velocity = anim->GetIncomingVelocity();
    clip.metadata.outgoing_velocity = anim->GetOutgoingVelocity();
    clip.metadata.incoming_body_angle = anim->GetIncomingBodyAngle();
    clip.metadata.outgoing_body_angle = anim->GetOutgoingBodyAngle();
    clip.metadata.difficulty =
        static_cast<float>(std::atof(anim->GetVariable("animdifficultyfactor").c_str()));
    clip.metadata.touch_frame = std::atoi(anim->GetVariable("touchframe").c_str());
    clip.metadata.quadrant = anim->GetVariableCache().quadrant_id();
    clip.metadata.action_type = static_cast<int32_t>(anim->GetAnimType());
    clip.metadata.last_ditch = anim->GetVariableCache().lastditch();
    clip.metadata.base_animation = anim->GetVariableCache().baseanim();
    clip.metadata.idle_level = anim->GetVariableCache().idlelevel();
    clip.metadata.special_var1 = anim->GetVariableCache().specialvar1();
    clip.metadata.special_var2 = anim->GetVariableCache().specialvar2();

    // Contact (first touch).
    if (clip.metadata.touch_frame >= 0) {
      Vector3 contact_pos;
      if (std::static_pointer_cast<FootballAnimationExtension>(
              anim->GetExtension("football"))
              ->GetTouchPos(clip.metadata.touch_frame, contact_pos)) {
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

int Verify(const std::vector<Animation*>& legacy,
          const AnimationLibrary& library) {
  if (legacy.size() != library.Size()) {
    std::cerr << "anim_baker: VERIFY FAILED: clip count " << legacy.size()
              << " vs " << library.Size() << "\n";
    return 1;
  }

  size_t mismatches = 0;
  for (size_t i = 0; i < legacy.size(); ++i) {
    Animation* anim = legacy[i];
    const AnimationClip& clip = library.Clips()[i];

    if (anim->GetName() != clip.name) {
      std::cerr << "VERIFY clip " << i << ": name mismatch\n";
      ++mismatches;
    }
    if (static_cast<uint32_t>(anim->GetFrameCount()) != clip.frame_count) {
      std::cerr << "VERIFY clip " << i << ": frame count mismatch\n";
      ++mismatches;
      continue;
    }

    for (uint32_t f = 0; f < clip.frame_count; ++f) {
      Quaternion o;
      Vector3 p;
      anim->GetKeyFrame(player, static_cast<int>(f), o, p);
      p.coords[2] = 0.0f;
      for (int c = 0; c < 3; ++c) {
        if (p.coords[c] != clip.root_positions[f].coords[c]) {
          std::cerr << "VERIFY clip " << i << " frame " << f
                    << ": root mismatch\n";
          ++mismatches;
          break;
        }
      }
      for (size_t b = 0; b < kBodyPartCount; ++b) {
        anim->GetKeyFrame(static_cast<BodyPart>(b), static_cast<int>(f), o, p);
        for (int e = 0; e < 4; ++e) {
          if (o.elements[e] != clip.poses[f].orientations[b].elements[e]) {
            std::cerr << "VERIFY clip " << i << " frame " << f
                      << " body " << b << ": orientation mismatch\n";
            ++mismatches;
          }
        }
        for (int c = 0; c < 3; ++c) {
          if (p.coords[c] != clip.poses[f].positions[b].coords[c]) {
            std::cerr << "VERIFY clip " << i << " frame " << f
                      << " body " << b << ": position mismatch\n";
            ++mismatches;
          }
        }
      }
    }

    const FootballAnimationMetadata& m = clip.metadata;
    if (m.action_type != static_cast<int32_t>(anim->GetAnimType()) ||
        m.incoming_velocity != anim->GetIncomingVelocity() ||
        m.outgoing_velocity != anim->GetOutgoingVelocity() ||
        m.incoming_body_angle != anim->GetIncomingBodyAngle() ||
        m.outgoing_body_angle != anim->GetOutgoingBodyAngle() ||
        m.difficulty !=
            static_cast<float>(
                std::atof(anim->GetVariable("animdifficultyfactor").c_str())) ||
        m.touch_frame != std::atoi(anim->GetVariable("touchframe").c_str()) ||
        m.quadrant != anim->GetVariableCache().quadrant_id() ||
        m.last_ditch != anim->GetVariableCache().lastditch() ||
        m.base_animation != anim->GetVariableCache().baseanim() ||
        m.idle_level != anim->GetVariableCache().idlelevel() ||
        m.special_var1 != anim->GetVariableCache().specialvar1() ||
        m.special_var2 != anim->GetVariableCache().specialvar2()) {
      std::cerr << "VERIFY clip " << i << ": metadata mismatch\n";
      ++mismatches;
    }

    Vector3 cp;
    const bool has = std::static_pointer_cast<FootballAnimationExtension>(
                         anim->GetExtension("football"))
                         ->GetTouchPos(m.touch_frame, cp);
    if (has != clip.has_contact ||
        (has && (cp.coords[0] != clip.contact_position.coords[0] ||
                 cp.coords[1] != clip.contact_position.coords[1] ||
                 cp.coords[2] != clip.contact_position.coords[2]))) {
      std::cerr << "VERIFY clip " << i << ": contact mismatch\n";
      ++mismatches;
    }
  }

  if (mismatches) {
    std::cerr << "anim_baker: VERIFY FAILED (" << mismatches << " mismatches)\n";
    return 1;
  }
  std::cout << "anim_baker: VERIFY PASSED (" << legacy.size()
            << " clips, all fields equivalent)\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  std::string input_dir;
  std::string out_path;
  std::string check_path;
  std::string verify_path;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--input" && i + 1 < argc) {
      input_dir = argv[++i];
    } else if (arg == "--out" && i + 1 < argc) {
      out_path = argv[++i];
    } else if (arg == "--check" && i + 1 < argc) {
      check_path = argv[++i];
    } else if (arg == "--verify" && i + 1 < argc) {
      verify_path = argv[++i];
    } else {
      std::cerr << "usage: " << argv[0]
                << " --input DIR [--out FILE] [--check FILE] [--verify FILE]\n";
      return 2;
    }
  }

  if (input_dir.empty()) {
    std::cerr << "anim_baker: --input DIR is required\n";
    return 2;
  }
  if (out_path.empty() && check_path.empty() && verify_path.empty()) {
    std::cerr << "anim_baker: specify --out, --check and/or --verify\n";
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
  if (!verify_path.empty()) {
    AnimationLibrary library;
    if (!library.Load(verify_path)) return 1;
    status |= Verify(anims.GetAnimations(), library);
  }
  return status;
}