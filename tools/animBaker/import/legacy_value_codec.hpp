#ifndef FOOTBALL_ANIMBAKER_IMPORT_LEGACY_VALUE_CODEC_HPP
#define FOOTBALL_ANIMBAKER_IMPORT_LEGACY_VALUE_CODEC_HPP

#include <string>

#include "football/foundation/math/quaternion.hpp"
#include "football/foundation/math/vector3.hpp"

namespace football::anim_baker {
using namespace football::math;

std::string GetStringFromVector(const Vector3& vec);
Vector3 GetVectorFromString(const std::string& value);
Quaternion GetQuaternionFromString(const std::string& value);

}  // namespace football::anim_baker

#endif  // FOOTBALL_ANIMBAKER_IMPORT_LEGACY_VALUE_CODEC_HPP
