#include "import/legacy_value_codec.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "import/legacy_text.hpp"

namespace blunted {

std::string GetStringFromVector(const Vector3& vec) {
  char buffer[1000];
  std::snprintf(buffer, sizeof(buffer), "%f, %f, %f", vec.coords[0],
                vec.coords[1], vec.coords[2]);
  return buffer;
}

Vector3 GetVectorFromString(const std::string& value) {
  if (value.empty()) {
    return Vector3(0.0f);
  }
  std::vector<std::string> tokens;
  tokenize(value, tokens, ",");
  assert(!tokens.empty());
  assert(tokens.size() <= 3);
  Vector3 vector;
  vector.coords[0] = std::atof(tokens.at(0).c_str());
  if (tokens.size() > 1) vector.coords[1] = std::atof(tokens.at(1).c_str());
  if (tokens.size() > 2) vector.coords[2] = std::atof(tokens.at(2).c_str());
  return vector;
}

Quaternion GetQuaternionFromString(const std::string& value) {
  std::vector<std::string> tokens;
  tokenize(value, tokens, ",");
  assert(tokens.size() == 4);
  const radian angle =
      std::atof(tokens.at(0).c_str()) / 360.0f * 2.0f * pi;
  Vector3 axis(std::atof(tokens.at(1).c_str()),
               std::atof(tokens.at(2).c_str()),
               std::atof(tokens.at(3).c_str()));
  Quaternion quaternion;
  quaternion.SetAngleAxis(angle, axis);
  return quaternion;
}

}  // namespace blunted
