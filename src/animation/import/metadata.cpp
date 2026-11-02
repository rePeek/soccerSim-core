#include "import.hpp"

#include <cmath>
#include <fstream>
#include <set>
#include <stdexcept>

namespace animation_import {
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

void ApplyEntry(ImportedAnimationClip& clip, const Json& entry) {
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
}  // namespace animation_import
