#ifndef FOOTBALL_SUPPORT_TEXT_VALUE_CODEC_HPP
#define FOOTBALL_SUPPORT_TEXT_VALUE_CODEC_HPP

#include "support/text/value_codec.hpp"
#include <string>

#include "foundation/math/quaternion.hpp"
#include "foundation/math/vector3.hpp"

namespace blunted {

std::string GetStringFromVector(const Vector3& vec);
Vector3 GetVectorFromString(const std::string& value);
Quaternion GetQuaternionFromString(const std::string& value);

}  // namespace blunted

#endif  // FOOTBALL_SUPPORT_TEXT_VALUE_CODEC_HPP
