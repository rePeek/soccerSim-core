#ifndef FOOTBALL_ANIMBAKER_IMPORT_LEGACY_VALUE_CODEC_HPP
#define FOOTBALL_ANIMBAKER_IMPORT_LEGACY_VALUE_CODEC_HPP

#include <string>

#include "foundation/math/quaternion.hpp"
#include "foundation/math/vector3.hpp"

namespace blunted {

std::string GetStringFromVector(const Vector3& vec);
Vector3 GetVectorFromString(const std::string& value);
Quaternion GetQuaternionFromString(const std::string& value);

}  // namespace blunted

#endif  // FOOTBALL_ANIMBAKER_IMPORT_LEGACY_VALUE_CODEC_HPP
