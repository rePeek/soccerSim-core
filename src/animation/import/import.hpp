#pragma once

#include <array>
#include <string_view>
#include <nlohmann/json.hpp>
#include "football/animation/library.hpp"
#include "foot_support.hpp"

namespace animation_import {
using Json = nlohmann::json;
namespace fs = std::filesystem;
inline constexpr std::array<std::string_view, 14> kNames{
    "middle", "neck", "left_thigh", "right_thigh", "left_knee", "right_knee",
    "left_ankle", "right_ankle", "left_shoulder", "right_shoulder",
    "left_elbow", "right_elbow", "body", "player"};
void Check(bool condition, const std::string& message);
Json ReadJson(const fs::path& path);
float Number(const Json& value);
int Integer(const Json& value);
Vector3 Vec(const Json& value);
FootballAnimationMetadata Metadata(const Json& j);
Json MetadataJson(const FootballAnimationMetadata& m);
void ApplyEntry(ImportedAnimationClip& clip, const Json& entry);
void MatchMetadata(const Json& a, const Json& b);
struct Skeleton {
  Json description;
  explicit Skeleton(const fs::path& path);
};
fs::path SourcePath(const fs::path& directory, const Json& file);
ImportedAnimationClip ReadGlb(const fs::path& path, const Json& entry, const Skeleton& skeleton);
ImportedAnimationClip Mirror(const ImportedAnimationClip& source);
void RepairGaitCycles(std::vector<ImportedAnimationClip>& clips, const std::vector<int>& partner);
struct MotionTemplate {
  ImportedAnimationClip clip;
  std::array<std::vector<int>, 14> keys;
  float incoming_angle = 0, outgoing_angle = 0;
  Vector3 incoming, outgoing;
};
std::vector<MotionTemplate> ReadTemplates(const fs::path& manifest, const fs::path& controls);
ImportedAnimationClip Generate(const Json& entry, const std::vector<MotionTemplate>& templates);
}  // namespace animation_import
