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

// Minimal first-stage animation baker.
//
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
#include "foundation/math/vector3.hpp"

using namespace blunted;

namespace {

constexpr char kMagic[8] = {'S', 'I', 'M', 'A', 'N', 'I', 'M', '1'};
constexpr uint32_t kVersion = 1;

struct ClipRecord {
  std::string name;
  uint32_t frame_count = 0;
  std::vector<Vector3> root_positions;  // z already zeroed, one per frame
  float incoming_velocity = 0.0f;
  float outgoing_velocity = 0.0f;
  float incoming_body_angle = 0.0f;
  float outgoing_body_angle = 0.0f;
  float anim_difficulty = 0.0f;
  int32_t touch_frame = 0;
  int32_t quadrant_id = 0;
  int32_t anim_type = 0;
};

void WriteU32(std::ostream& os, uint32_t v) { os.write(reinterpret_cast<const char*>(&v), sizeof(v)); }
void WriteI32(std::ostream& os, int32_t v) { os.write(reinterpret_cast<const char*>(&v), sizeof(v)); }
void WriteF32(std::ostream& os, float v) { os.write(reinterpret_cast<const char*>(&v), sizeof(v)); }
void WriteString(std::ostream& os, const std::string& s) {
  WriteU32(os, static_cast<uint32_t>(s.size()));
  os.write(s.data(), static_cast<std::streamsize>(s.size()));
}

uint32_t ReadU32(std::istream& is) {
  uint32_t v = 0;
  is.read(reinterpret_cast<char*>(&v), sizeof(v));
  return v;
}
int32_t ReadI32(std::istream& is) {
  int32_t v = 0;
  is.read(reinterpret_cast<char*>(&v), sizeof(v));
  return v;
}
float ReadF32(std::istream& is) {
  float v = 0.0f;
  is.read(reinterpret_cast<char*>(&v), sizeof(v));
  return v;
}
std::string ReadString(std::istream& is) {
  const uint32_t len = ReadU32(is);
  std::string s(len, '\0');
  if (len > 0) is.read(s.data(), len);
  return s;
}


std::vector<ClipRecord> BakeClips(const std::vector<Animation*>& animations) {

  std::vector<ClipRecord> clips;
  clips.reserve(animations.size());

  for (Animation* anim : animations) {
    ClipRecord clip;
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

    clip.root_positions.reserve(clip.frame_count);
    Quaternion orientation;  // unused; root orientation is not baked yet
    Vector3 position;
    for (uint32_t frame = 0; frame < clip.frame_count; ++frame) {
      anim->GetKeyFrame(player, static_cast<int>(frame), orientation, position);
      position.coords[2] = 0.0f;
      clip.root_positions.push_back(position);
    }

    clips.push_back(std::move(clip));
  }
  return clips;
}

void Serialize(std::ostream& os, const std::vector<ClipRecord>& clips) {
  os.write(kMagic, sizeof(kMagic));
  WriteU32(os, kVersion);
  WriteU32(os, static_cast<uint32_t>(clips.size()));

  for (const ClipRecord& clip : clips) {
    WriteString(os, clip.name);
    WriteU32(os, clip.frame_count);
    for (const Vector3& p : clip.root_positions) {
      WriteF32(os, p.coords[0]);
      WriteF32(os, p.coords[1]);
      WriteF32(os, p.coords[2]);
    }
    WriteF32(os, clip.incoming_velocity);
    WriteF32(os, clip.outgoing_velocity);
    WriteF32(os, clip.incoming_body_angle);
    WriteF32(os, clip.outgoing_body_angle);
    WriteF32(os, clip.anim_difficulty);
    WriteI32(os, clip.touch_frame);
    WriteI32(os, clip.quadrant_id);
    WriteI32(os, clip.anim_type);
  }
}

std::vector<char> SerializeToBuffer(const std::vector<ClipRecord>& clips) {
  std::ostringstream oss(std::ios::binary);
  Serialize(oss, clips);
  const std::string s = oss.str();
  return std::vector<char>(s.begin(), s.end());
}

bool ReadHeaderAndVerify(std::istream& is, uint32_t& clip_count) {
  char magic[8] = {};
  is.read(magic, sizeof(magic));
  if (std::memcmp(magic, kMagic, sizeof(magic)) != 0) {
    std::cerr << "anim_baker: bad magic\n";
    return false;
  }
  const uint32_t version = ReadU32(is);
  if (version != kVersion) {
    std::cerr << "anim_baker: unsupported version " << version << "\n";
    return false;
  }
  clip_count = ReadU32(is);
  return true;
}

int Export(const std::string& path, const std::vector<ClipRecord>& clips) {
  std::ofstream os(path, std::ios::binary | std::ios::trunc);
  if (!os) {
    std::cerr << "anim_baker: cannot open " << path << " for writing\n";
    return 1;
  }
  Serialize(os, clips);
  std::cout << "anim_baker: wrote " << clips.size() << " clips to " << path
            << " (" << sizeof(kMagic) + 8 << " header bytes + clip data)\n";
  return 0;
}

int Check(const std::string& path, const std::vector<ClipRecord>& baked) {
  std::ifstream is(path, std::ios::binary);
  if (!is) {
    std::cerr << "anim_baker: cannot open " << path << " for reading\n";
    return 1;
  }
  uint32_t clip_count = 0;
  if (!ReadHeaderAndVerify(is, clip_count)) return 1;

  // Re-derive the expected bytes from the freshly baked clips and compare.
  const std::vector<char> expected = SerializeToBuffer(baked);
  is.clear();
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
  std::cout << "anim_baker: CHECK PASSED (" << clip_count << " clips, "
            << expected.size() << " bytes, byte-identical)\n";
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

  const std::vector<ClipRecord> clips = BakeClips(anims.GetAnimations());

  int status = 0;
  if (!out_path.empty()) status |= Export(out_path, clips);
  if (!check_path.empty()) status |= Check(check_path, clips);
  return status;
}