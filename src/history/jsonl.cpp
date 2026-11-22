#include "football/history/history.hpp"

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

namespace football::history {

namespace {

using Json = nlohmann::ordered_json;

constexpr int kFormatVersion = 1;

[[noreturn]] void Fail(const std::filesystem::path& path, std::size_t line,
                       const std::string& reason) {
  throw std::runtime_error("history " + path.string() + ":" +
                           std::to_string(line) + ": " + reason);
}

Json MakeVectorArray(const math::Vector3& vector) {
  Json array = Json::array();
  for (const float coordinate : vector.coords) array.push_back(coordinate);
  return array;
}

Json MakeOrientationArray(const math::Quaternion& orientation) {
  Json array = Json::array();
  for (const float element : orientation.elements) array.push_back(element);
  return array;
}

Json MakeHeader() {
  return Json{{"type", "header"},
              {"version", kFormatVersion},
              {"ticks_per_second", time::kTicksPerSecond}};
}

Json MakeSnapshot(const Snapshot& snapshot) {
  return Json{{"type", "snapshot"},
              {"tick", snapshot.tick.value},
              {"position", MakeVectorArray(snapshot.ball.position)},
              {"velocity", MakeVectorArray(snapshot.ball.velocity)},
              {"angular_velocity", MakeVectorArray(snapshot.ball.angular_velocity)},
              {"orientation", MakeOrientationArray(snapshot.ball.orientation)}};
}

const Json& Member(const std::filesystem::path& path, std::size_t line,
                   const Json& object, const char* key) {
  const auto member = object.find(key);
  if (member == object.end()) Fail(path, line, std::string("missing ") + key);
  return *member;
}

const Json& MemberArray(const std::filesystem::path& path, std::size_t line,
                        const Json& object, const char* key, std::size_t size) {
  const Json& member = Member(path, line, object, key);
  if (!member.is_array()) Fail(path, line, std::string(key) + " is not an array");
  if (member.size() != size) Fail(path, line, std::string(key) + " has the wrong length");
  return member;
}

math::Vector3 ReadVector(const std::filesystem::path& path, std::size_t line,
                         const Json& object, const char* key) {
  const Json& array = MemberArray(path, line, object, key, 3);
  return math::Vector3(array.at(0).get<float>(), array.at(1).get<float>(),
                       array.at(2).get<float>());
}

math::Quaternion ReadOrientation(const std::filesystem::path& path, std::size_t line,
                                 const Json& object) {
  const Json& array = MemberArray(path, line, object, "orientation", 4);
  return math::Quaternion(array.at(0).get<float>(), array.at(1).get<float>(),
                          array.at(2).get<float>(), array.at(3).get<float>());
}

void ReadHeader(const std::filesystem::path& path, std::size_t line,
                const Json& header) {
  if (Member(path, line, header, "type").get<std::string>() != "header") {
    Fail(path, line, "the first line must be a header");
  }
  const Json& version = Member(path, line, header, "version");
  if (!version.is_number_integer() || version.get<int>() != kFormatVersion) {
    Fail(path, line, "unsupported format version");
  }
  const Json& ticks = Member(path, line, header, "ticks_per_second");
  if (!ticks.is_number_unsigned() ||
      ticks.get<std::uint64_t>() != time::kTicksPerSecond) {
    Fail(path, line, "unsupported tick rate");
  }
}

Snapshot ReadSnapshot(const std::filesystem::path& path, std::size_t line,
                      const Json& value, std::uint64_t expected_tick) {
  if (Member(path, line, value, "type").get<std::string>() != "snapshot") {
    Fail(path, line, "expected a snapshot line");
  }
  const Json& tick = Member(path, line, value, "tick");
  if (!tick.is_number_unsigned() || tick.get<std::uint64_t>() != expected_tick) {
    Fail(path, line, "snapshots must start at tick zero and be consecutive");
  }
  Snapshot snapshot;
  snapshot.tick = time::Tick{expected_tick};
  snapshot.ball.position = ReadVector(path, line, value, "position");
  snapshot.ball.velocity = ReadVector(path, line, value, "velocity");
  snapshot.ball.angular_velocity = ReadVector(path, line, value, "angular_velocity");
  snapshot.ball.orientation = ReadOrientation(path, line, value);
  return snapshot;
}

Chunk ParseChunk(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("history could not open " + path.string());
  }

  Chunk chunk;
  std::size_t line_number = 0;
  for (std::string text; std::getline(input, text);) {
    ++line_number;
    const Json value = Json::parse(text, nullptr, false);
    if (value.is_discarded() || !value.is_object()) {
      Fail(path, line_number, "malformed JSON object");
    }
    if (line_number == 1) {
      ReadHeader(path, line_number, value);
      continue;
    }
    chunk.frames.push_back(
        ReadSnapshot(path, line_number, value, chunk.frames.size()));
  }
  if (!input.eof()) {
    throw std::runtime_error("history could not read " + path.string());
  }
  if (line_number == 0) {
    throw std::runtime_error("history file is empty: " + path.string());
  }
  return chunk;
}

std::filesystem::path TemporaryPathFor(const std::filesystem::path& path) {
  std::filesystem::path temporary = path;
  temporary += ".tmp";
  return temporary;
}

void ReplaceFile(const std::filesystem::path& temporary,
                 const std::filesystem::path& path) {
  std::error_code error;
  std::filesystem::rename(temporary, path, error);
  if (!error) return;
  std::filesystem::remove(path, error);
  std::filesystem::rename(temporary, path);
}

void RemoveQuietly(const std::filesystem::path& path) {
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

void WriteLines(const std::filesystem::path& temporary, const Chunk& chunk) {
  std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
  if (!output) {
    throw std::runtime_error("history could not open " + temporary.string());
  }

  output << MakeHeader().dump() << '\n';
  for (const Snapshot& snapshot : chunk.frames) {
    output << MakeSnapshot(snapshot).dump() << '\n';
  }
  output.flush();
  if (!output) {
    throw std::runtime_error("history could not write " + temporary.string());
  }
}

}

History History::LoadJsonl(const std::filesystem::path& path) {
  return History(ParseChunk(path));
}

void History::SaveJsonl(const std::filesystem::path& path) const {
  const std::filesystem::path temporary = TemporaryPathFor(path);
  try {
    WriteLines(temporary, chunk_);
    ReplaceFile(temporary, path);
  } catch (...) {
    RemoveQuietly(temporary);
    throw;
  }
}

}
