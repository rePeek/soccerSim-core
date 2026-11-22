#include "football/history/history.hpp"

#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>

#include <nlohmann/json.hpp>

namespace football::history {

namespace {

using Json = nlohmann::ordered_json;

constexpr int kFormatVersion = 1;

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

void History::SaveJsonl(const std::filesystem::path& path) const {
  std::lock_guard lock(mutex_);

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
