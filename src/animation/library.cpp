// Canonical GLB catalog import. No Player, legacy importer or disk cache needed.
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include <nlohmann/json.hpp>

#include "football/animation/library.hpp"
#include "football/animation/foot_support.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string_view>

namespace {
using Json = nlohmann::json;
namespace fs = std::filesystem;
constexpr std::array<std::string_view, 14> kNames{
    "middle", "neck", "left_thigh", "right_thigh", "left_knee", "right_knee",
    "left_ankle", "right_ankle", "left_shoulder", "right_shoulder",
    "left_elbow", "right_elbow", "body", "player"};
constexpr std::array<int, 14> kMirror{0, 1, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 12, 13};

void Check(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}
Json ReadJson(const fs::path& path) {
  std::ifstream stream(path);
  Check(bool(stream), "cannot open " + path.string());
  std::vector<std::set<std::string>> keys;
  return Json::parse(stream, [&](int, Json::parse_event_t event, Json& value) {
    if (event == Json::parse_event_t::object_start) keys.emplace_back();
    if (event == Json::parse_event_t::key)
      Check(keys.back().insert(value.get<std::string>()).second, "duplicate JSON key");
    if (event == Json::parse_event_t::object_end) keys.pop_back();
    if (event == Json::parse_event_t::value && value.is_number_float())
      Check(std::isfinite(value.get<double>()), "non-finite JSON value");
    return true;
  });
}
float Number(const Json& value) {
  Check(value.is_number(), "expected numeric value");
  const float result = value.get<float>();
  Check(std::isfinite(result), "non-finite/out-of-range number");
  return result;
}
int Integer(const Json& value) {
  Check(value.is_number_integer(), "expected integer");
  const auto result = value.get<std::int64_t>();
  Check(result >= INT32_MIN && result <= INT32_MAX, "integer out of range");
  return static_cast<int>(result);
}
Vector3 Vec(const Json& value) {
  Check(value.is_array() && value.size() == 3, "expected vector3");
  return {Number(value[0]), Number(value[1]), Number(value[2])};
}
Json ToJson(const Vector3& v) { return {v.coords[0], v.coords[1], v.coords[2]}; }

#define FLOAT_FIELDS(X) \
 X(incoming_velocity) X(outgoing_velocity) X(incoming_body_angle) \
 X(outgoing_body_angle) X(outgoing_angle) X(difficulty) X(idle_level) \
 X(special_var1) X(special_var2) X(incoming_ball_direction_max_deviation) \
 X(outgoing_ball_direction_max_deviation) X(touch_max_power_factor) \
 X(touch_difficulty_factor) X(priority)
#define INT_FIELDS(X) X(action_type) X(touch_frame) X(quadrant) X(trip_type) X(current_foot) X(outgoing_foot)
#define BOOL_FIELDS(X) X(last_ditch) X(base_animation)
#define STRING_FIELDS(X) X(incoming_retain_state) X(outgoing_retain_state) X(incoming_special_state) \
 X(forced_foot) X(touch_foot) X(outgoing_special_state) X(touch_bodypart)
#define VECTOR_FIELDS(X) X(incoming_body_direction) X(outgoing_direction) X(incoming_ball_direction) \
 X(outgoing_ball_direction) X(bump_direction) X(incoming_movement) X(outgoing_movement) X(translation)

FootballAnimationMetadata Metadata(const Json& j) {
  FootballAnimationMetadata m;
  std::size_t fields = 0;
#define READ_FLOAT(n) m.n = Number(j.at(#n)); ++fields;
#define READ_INT(n) m.n = Integer(j.at(#n)); ++fields;
#define READ_BOOL(n) m.n = j.at(#n).get<bool>(); ++fields;
#define READ_STRING(n) m.n = j.at(#n).get<std::string>(); ++fields;
#define READ_VECTOR(n) m.n = Vec(j.at(#n)); ++fields;
  FLOAT_FIELDS(READ_FLOAT) INT_FIELDS(READ_INT) BOOL_FIELDS(READ_BOOL)
  STRING_FIELDS(READ_STRING) VECTOR_FIELDS(READ_VECTOR)
#undef READ_FLOAT
#undef READ_INT
#undef READ_BOOL
#undef READ_STRING
#undef READ_VECTOR
  Check(j.is_object() && j.size() == fields, "unknown football metadata");
  Check(m.current_foot >= 0 && m.current_foot <= 1 && m.outgoing_foot >= 0 && m.outgoing_foot <= 1,
        "invalid foot enum");
  return m;
}
Json MetadataJson(const FootballAnimationMetadata& m) {
  Json j = Json::object();
#define SAVE(n) j[#n] = m.n;
#define SAVE_VECTOR(n) j[#n] = ToJson(m.n);
  FLOAT_FIELDS(SAVE) INT_FIELDS(SAVE) BOOL_FIELDS(SAVE) STRING_FIELDS(SAVE)
  VECTOR_FIELDS(SAVE_VECTOR)
#undef SAVE
#undef SAVE_VECTOR
  return j;
}
#undef FLOAT_FIELDS
#undef INT_FIELDS
#undef BOOL_FIELDS
#undef STRING_FIELDS
#undef VECTOR_FIELDS

void ApplyEntry(AnimationClip& clip, const Json& entry) {
  clip.id = Integer(entry.at("id"));
  clip.name = entry.at("name").get<std::string>();
  const int frames = Integer(entry.at("frame_count"));
  Check(frames > 0 && frames <= 1000000 && clip.poses.size() == std::size_t(frames),
        "frame count mismatch for " + clip.name);
  clip.frame_count = frames;
  clip.metadata = Metadata(entry.at("metadata"));
  Check(clip.metadata.touch_frame >= -1 && clip.metadata.touch_frame < frames, "invalid contact frame");
  clip.has_contact = !entry.at("contact_position").is_null();
  clip.contact_position = clip.has_contact ? Vec(entry.at("contact_position")) : Vector3(0);
  Check(!clip.has_contact || clip.metadata.touch_frame >= 0, "contact without contact frame");
  clip.touches.clear();
  for (const auto& touch : entry.at("touches")) {
    const int frame = Integer(touch.at("frame"));
    Check(frame >= 0 && frame < frames, "touch outside clip");
    clip.touches.push_back({frame, Vec(touch.at("position"))});
  }
}

struct Skeleton {
  Json description;
  explicit Skeleton(const fs::path& path) : description(ReadJson(path)) {
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
};

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
AnimationClip ReadGlb(const fs::path& path, const Json& entry, const Skeleton& skeleton) {
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
  AnimationClip clip;
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

// Same reflection as the source compiler, including metadata not used by poses.
std::string MirrorString(std::string value) {
  if (value.starts_with("left")) return "right" + value.substr(4);
  if (value.starts_with("right")) return "left" + value.substr(5);
  return value;
}
AnimationClip Mirror(const AnimationClip& source) {
  AnimationClip clip = source;
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
void MatchMetadata(const Json& a, const Json& b) {
  Check(a.type() == b.type() || (a.is_number() && b.is_number()), "mirror metadata type mismatch");
  if (a.is_object()) {
    Check(a.size() == b.size(), "mirror metadata fields mismatch");
    for (auto it = a.begin(); it != a.end(); ++it) MatchMetadata(it.value(), b.at(it.key()));
  } else if (a.is_array()) {
    Check(a.size() == b.size(), "mirror metadata size mismatch");
    for (std::size_t i = 0; i < a.size(); ++i) MatchMetadata(a[i], b[i]);
  } else if (a.is_number()) Check(std::fabs(a.get<double>() - b.get<double>()) <= 2e-5, "mirror metadata mismatch");
  else Check(a == b, "mirror metadata mismatch");
}

struct MotionTemplate {
  AnimationClip clip;
  std::array<std::vector<int>, 14> keys;
  float incoming_angle = 0, outgoing_angle = 0;
  Vector3 incoming, outgoing;
};
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
AnimationClip Generate(const Json& entry, const std::vector<MotionTemplate>& templates) {
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
  AnimationClip clip;
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

}  // namespace

bool AnimationLibrary::Load(const std::filesystem::path& manifest) {
  try {
    const auto directory = fs::absolute(manifest).parent_path();
    const auto doc = ReadJson(manifest);
    Check(doc.at("schema") == "football_animation_catalog_v1" &&
          doc.at("rig_version") == "football_player_v1" && doc.at("fps") == 100, "unsupported catalog");
    Skeleton skeleton(directory / doc.at("skeleton").get<std::string>());
    const auto& entries = doc.at("clips");
    Check(entries.is_array() && !entries.empty() && entries.size() <= 100000, "invalid catalog size");
    bool procedural = false;
    for (std::size_t i = 0; i < entries.size(); ++i) {
      const auto& e = entries[i];
      Check(Integer(e.at("id")) == static_cast<int>(i), "non-contiguous clip IDs");
      const int kinds = int(e.contains("file")) + int(e.value("procedural", false)) + int(e.contains("mirror_of"));
      Check(kinds == 1, "clip must have exactly one source kind");
      procedural |= e.value("procedural", false);
    }
    std::vector<MotionTemplate> templates;
    if (procedural) templates = ReadTemplates(directory / doc.at("motion_templates").get<std::string>(),
                                              directory / doc.at("generation_keys").get<std::string>());
    std::vector<AnimationClip> clips(entries.size());
    std::set<fs::path> paths;
    for (std::size_t i = 0; i < entries.size(); ++i) {
      const auto& e = entries[i];
      if (e.contains("mirror_of")) continue;
      if (e.value("procedural", false)) clips[i] = Generate(e, templates);
      else {
        const auto path = SourcePath(directory, e.at("file"));
        Check(paths.insert(path).second, "duplicate GLB source path");
        clips[i] = ReadGlb(path, e, skeleton);
      }
    }
    for (std::size_t i = 0; i < entries.size(); ++i) {
      const auto& e = entries[i];
      if (!e.contains("mirror_of")) continue;
      const int source = Integer(e.at("mirror_of"));
      Check(source >= 0 && std::size_t(source) < entries.size() && source != int(i) &&
            !entries[source].contains("mirror_of"), "invalid mirror reference/chain");
      clips[i] = Mirror(clips[source]);
      MatchMetadata(MetadataJson(clips[i].metadata), MetadataJson(Metadata(e.at("metadata"))));
      ApplyEntry(clips[i], e);
    }
    for (auto& clip : clips) {
      DeriveFootSupport(clip);
      // RMA-1: report whether the source root and support foot agree before
      // any Player can choose the IK-free adaptation for this clip.
      DeriveMotionQuality(clip);
    }
    clips_ = std::move(clips);
    return true;
  } catch (const std::exception& error) {
    std::cerr << "AnimationLibrary: " << manifest << ": " << error.what() << '\n';
    return false;
  }
}
