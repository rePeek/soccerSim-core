#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include "import.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>

namespace animation_import {
Skeleton::Skeleton(const fs::path& path) : description(ReadJson(path)) {
    Check(description.at("rig_version") == "football_player_v1", "unsupported rig");
    const auto& bones = description.at("bones");
    Check(bones.size() == kNames.size(), "expected 14 bones");
    std::set<std::string> seen;
    for (const auto& bone : bones) {
      const auto name = bone.at("name").get<std::string>();
      Check(std::find(kNames.begin(), kNames.end(), name) != kNames.end() && seen.insert(name).second,
            "duplicate/unknown bone");
      if (bone.at("parent").is_null()) Check(name == "player", "invalid root");
      else Check(seen.contains(bone.at("parent").get<std::string>()), "skeleton must be parent-first");
      // Runtime FK still uses this canonical bind contract; reject silent drift.
      const auto part = static_cast<BodyPart>(std::find(kNames.begin(), kNames.end(), name) - kNames.begin());
      const auto offset = Vec(bone.at("offset_zup"));
      if (part != player) {
        Check(bone.at("parent") == kNames[PlayerBindParent(part)], "unsupported bind parent");
        Check((offset - PlayerBindOffset(part)).GetLength() < 1e-7f, "unsupported bind offset");
      } else Check(bone.at("parent").is_null() && offset.GetLength() == 0, "invalid root bind");
    }
}

fs::path SourcePath(const fs::path& directory, const Json& file) {
  const auto relative = fs::path(file.get<std::string>());
  Check(!relative.is_absolute() && relative.extension() == ".glb", "invalid GLB path");
  const auto path = fs::weakly_canonical(directory / relative);
  const auto rel = path.lexically_relative(fs::canonical(directory));
  Check(!rel.empty() && *rel.begin() != "..", "GLB escapes catalog directory");
  return path;
}
void Identity(const cgltf_node& node) {
  Check(!node.has_matrix, "matrix rest transform not supported");
  for (int i = 0; i < 4; ++i)
    Check(!node.has_rotation || node.rotation[i] == (i == 3 ? 1.0f : 0.0f), "non-canonical rest rotation");
  for (int i = 0; i < 3; ++i)
    Check(!node.has_scale || node.scale[i] == 1.0f, "non-canonical rest scale");
}
void Accessor(const cgltf_accessor* a, cgltf_type type, std::size_t count) {
  Check(a && a->type == type && a->component_type == cgltf_component_type_r_32f &&
        !a->is_sparse && !a->normalized && a->count == count && a->buffer_view &&
        !a->buffer_view->has_meshopt_compression, "unsupported animation accessor");
  const auto& view = *a->buffer_view;
  const std::size_t width = cgltf_num_components(type) * sizeof(float);
  Check(a->stride >= width && a->stride % 4 == 0 && a->offset % 4 == 0 && view.offset % 4 == 0,
        "invalid animation accessor alignment/stride");
  Check(view.buffer && view.offset <= view.buffer->size && view.size <= view.buffer->size-view.offset &&
        a->offset <= view.size && width <= view.size-a->offset &&
        count-1 <= (view.size-a->offset-width)/a->stride, "animation accessor exceeds buffer bounds");
}
void ReadValues(const cgltf_accessor* a, std::size_t frame, float* values, std::size_t size) {
  Check(cgltf_accessor_read_float(a, frame, values, size), "cannot read animation sample");
  for (std::size_t i = 0; i < size; ++i) Check(std::isfinite(values[i]), "non-finite animation sample");
}
ImportedAnimationClip ReadGlb(const fs::path& path, const Json& entry, const Skeleton& skeleton) {
  cgltf_options options{};
  cgltf_data* raw = nullptr;
  const auto result = cgltf_parse_file(&options, path.string().c_str(), &raw);
  std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(raw, cgltf_free);
  Check(result == cgltf_result_success, "cannot parse GLB " + path.string());
  Check(data->file_type == cgltf_file_type_glb && data->buffers_count == 1 &&
        !data->buffers[0].uri && data->extensions_required_count == 0,
        "expected uncompressed GLB with one embedded buffer");
  Check(cgltf_load_buffers(&options, data.get(), nullptr) == cgltf_result_success &&
        cgltf_validate(data.get()) == cgltf_result_success, "invalid GLB buffers/layout");
  Check(data->animations_count == 1, "expected one animation");
  Check(data->extras.data && Json::parse(data->extras.data).at("rig_version") == "football_player_v1",
        "missing canonical rig version");
  const auto& animation = data->animations[0];
  Check(animation.name && entry.at("name") == animation.name, "animation name mismatch");
  const int frames = Integer(entry.at("frame_count"));
  Check(frames > 0 && frames <= 1000000, "invalid frame count");
  std::array<const cgltf_node*, 14> nodes{};
  for (std::size_t i = 0; i < data->nodes_count; ++i) {
    const auto& node = data->nodes[i];
    if (!node.name) continue;
    const auto it = std::find(kNames.begin(), kNames.end(), node.name);
    if (it == kNames.end()) continue;
    const auto index = it - kNames.begin();
    Check(!nodes[index], "duplicate bone node");
    nodes[index] = &node;
  }
  for (const auto& bone : skeleton.description.at("bones")) {
    const auto name = bone.at("name").get<std::string>();
    const auto index = std::find(kNames.begin(), kNames.end(), name) - kNames.begin();
    Check(nodes[index], "missing bone " + name);
    const auto& node = *nodes[index];
    Identity(node);
    const auto offset = Vec(bone.at("offset_zup"));
    const std::array<float, 3> yup{offset.coords[0], offset.coords[2], -offset.coords[1]};
    for (int j = 0; j < 3; ++j)
      Check(std::isfinite(node.translation[j]) && std::fabs(node.translation[j] - yup[j]) < 1e-7f,
            "rest offset mismatch");
    Check(node.parent, "missing skeleton object/parent");
    if (!bone.at("parent").is_null())
      Check(node.parent->name && bone.at("parent") == node.parent->name, "bone parent mismatch");
  }
  // Every ancestor above the logical root must be identity and acyclic.
  std::set<const cgltf_node*> ancestors{nodes[player]};
  for (auto* ancestor = nodes[player]->parent; ancestor; ancestor = ancestor->parent) {
    Check(ancestors.insert(ancestor).second, "cyclic skeleton hierarchy");
    Identity(*ancestor);
    for (float v : ancestor->translation) Check(v == 0, "non-identity skeleton object");
  }
  ImportedAnimationClip clip;
  clip.poses.resize(frames);
  clip.root_positions.resize(frames);
  std::array<bool, 14> rotations{};
  bool translation = false;
  for (std::size_t c = 0; c < animation.channels_count; ++c) {
    const auto& channel = animation.channels[c];
    const auto it = std::find(nodes.begin(), nodes.end(), channel.target_node);
    Check(channel.target_node && it != nodes.end(), "unsupported animation target");
    const auto joint = it - nodes.begin();
    const bool rotation = channel.target_path == cgltf_animation_path_type_rotation;
    Check(rotation || (joint == player && channel.target_path == cgltf_animation_path_type_translation),
          "unsupported channel (only rotations and root translation allowed)");
    if (rotation) { Check(!rotations[joint], "duplicate rotation"); rotations[joint] = true; }
    else { Check(!translation, "duplicate root translation"); translation = true; }
    Check(channel.sampler && channel.sampler->interpolation == cgltf_interpolation_type_linear,
          "expected LINEAR samples");
    const auto& sampler = *channel.sampler;
    Accessor(sampler.input, cgltf_type_scalar, frames);
    Accessor(sampler.output, rotation ? cgltf_type_vec4 : cgltf_type_vec3, frames);
    for (int frame = 0; frame < frames; ++frame) {
      float time[1], values[4];
      ReadValues(sampler.input, frame, time, 1);
      Check(std::fabs(time[0] - frame / 100.0f) <= 2e-6f, "expected 100 Hz samples from zero");
      ReadValues(sampler.output, frame, values, rotation ? 4 : 3);
      if (rotation) {
        float norm = 0;
        for (float v : values) norm += v * v;
        Check(std::fabs(norm - 1) <= 1e-4f, "invalid rotation quaternion");
        clip.poses[frame].orientations[joint].Set(values[0], -values[2], values[1], values[3]);
      } else {
        const Vector3 zup(values[0], -values[2], values[1]);
        clip.poses[frame].positions[player] = zup;
        clip.root_positions[frame] = zup.Get2D();
      }
    }
  }
  Check(translation && std::all_of(rotations.begin(), rotations.end(), [](bool b) { return b; }),
        "missing required animation channels");
  ApplyEntry(clip, entry);
  return clip;
}
}  // namespace animation_import
