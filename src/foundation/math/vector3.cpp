#include "football/foundation/math/vector3.hpp"

#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>

#include "football/foundation/math/quaternion.hpp"

namespace football::math {

Vector3::Vector3() {
  coords[0] = 0;
  coords[1] = 0;
  coords[2] = 0;
}

Vector3::Vector3(real xyz) {
  coords[0] = xyz;
  coords[1] = xyz;
  coords[2] = xyz;
}

Vector3::Vector3(real x, real y, real z) {
  coords[0] = x;
  coords[1] = y;
  coords[2] = z;
}

void Vector3::Set(real xyz) {
  coords[0] = xyz;
  coords[1] = xyz;
  coords[2] = xyz;
}

void Vector3::Set(real x, real y, real z) {
  coords[0] = x;
  coords[1] = y;
  coords[2] = z;
}

void Vector3::Set(const Vector3& vector) {
  coords[0] = vector.coords[0];
  coords[1] = vector.coords[1];
  coords[2] = vector.coords[2];
}

float Vector3::GetCoord(int index) const {
  switch (index) {
    case 0: return coords[0];
    case 1: return coords[1];
    case 2: return coords[2];
    default: assert(false && "invalid coordinate index");
  }
  return 0;
}

void Vector3::SetCoord(int index, float value) {
  switch (index) {
    case 0: coords[0] = value; break;
    case 1: coords[1] = value; break;
    case 2: coords[2] = value; break;
    default: assert(false && "invalid coordinate index");
  }
}

Vector3 Vector3::FromRotatedNegativeZAxis(const Quaternion& orientation) {
  Quaternion negative_z_axis_quaternion;
  negative_z_axis_quaternion.Set(0, 0, -1, 0);
  Quaternion rotated_axis =
      orientation * negative_z_axis_quaternion * orientation.GetInverse();
  return Vector3(rotated_axis.elements[0], rotated_axis.elements[1], rotated_axis.elements[2]);
}

Vector3 Vector3::GetCrossProduct(const Vector3& other) const {
  return Vector3(coords[1] * other.coords[2] - coords[2] * other.coords[1],
                 coords[2] * other.coords[0] - coords[0] * other.coords[2],
                 coords[0] * other.coords[1] - coords[1] * other.coords[0]);
}

real Vector3::GetDotProduct(const Vector3& other) const {
  return (coords[0] * other.coords[0] + coords[1] * other.coords[1] + coords[2] * other.coords[2]);
}

void Vector3::FastNormalize() {
  const real squared_length = GetDotProduct(*this);
  if (squared_length == 0.0f) return;

  constexpr std::uint32_t kInverseSquareRootSeed = 0x5f3759dfu;
  float inverse_length = squared_length;
  const float half_squared_length = 0.5f * inverse_length;
  std::uint32_t inverse_length_bits = std::bit_cast<std::uint32_t>(inverse_length);
  inverse_length_bits = kInverseSquareRootSeed - (inverse_length_bits >> 1);
  inverse_length = std::bit_cast<float>(inverse_length_bits);
  inverse_length *= 1.5f - half_squared_length * inverse_length * inverse_length;

  coords[0] *= inverse_length;
  coords[1] *= inverse_length;
  coords[2] *= inverse_length;
}

void Vector3::Normalize(const Vector3& fallback) {
  if (fabs(this->coords[0]) < 0.000001f &&
      fabs(this->coords[1]) < 0.000001f &&
      fabs(this->coords[2]) < 0.000001f) {
    this->coords[0] = fallback.coords[0];
    this->coords[1] = fallback.coords[1];
    this->coords[2] = fallback.coords[2];
  } else {
    real inverse_length = 1.0f / std::sqrt(GetDotProduct(*this));
    this->coords[0] *= inverse_length;
    this->coords[1] *= inverse_length;
    this->coords[2] *= inverse_length;
  }
}

void Vector3::Normalize() {
  const real squared_length = GetDotProduct(*this);
  if (squared_length == 0.0f) return;
  real inverse_length = 1.0f / std::sqrt(squared_length);
  coords[0] *= inverse_length;
  coords[1] *= inverse_length;
  coords[2] *= inverse_length;
}

void Vector3::NormalizeTo(float length) {
  const real squared_length = GetDotProduct(*this);
  if (squared_length == 0.0f) return;
  real length_scale = length / std::sqrt(squared_length);
  coords[0] *= length_scale;
  coords[1] *= length_scale;
  coords[2] *= length_scale;
}

void Vector3::ClampLength(float max_length) {
  if (GetLength() > max_length) {
    Normalize();
    *this *= max_length;
  }
}

Vector3 Vector3::GetNormalized(const Vector3& fallback) const {
  Vector3 normalized(*this);
  normalized.Normalize(fallback);
  return normalized;
}

Vector3 Vector3::GetNormalized() const {
  Vector3 normalized(*this);
  normalized.Normalize();
  return normalized;
}

Vector3 Vector3::GetNormalizedTo(float length) const {
  Vector3 normalized(*this);
  normalized.NormalizeTo(length);
  return normalized;
}

Vector3 Vector3::GetLengthClamped(float max_length) const {
  if (GetLength() > max_length) return GetNormalized(0) * max_length;
  return Vector3(*this);
}

real Vector3::GetDistance(const Vector3& other) const {
  real difference[3];
  difference[0] = coords[0] - other.coords[0];
  difference[1] = coords[1] - other.coords[1];
  difference[2] = coords[2] - other.coords[2];
  if (difference[0] == 0.0f && difference[1] == 0.0f && difference[2] == 0.0f) return 0.0f;

  float length = sqrt(std::pow(difference[0], 2) + std::pow(difference[1], 2) +
                      std::pow(difference[2], 2));
  if (length < 0.000001) length = 0;
  return length;
}

bool Vector3::Compare(const Vector3& other) const {
  return coords[0] == other.coords[0] && coords[1] == other.coords[1] &&
         coords[2] == other.coords[2];
}

Vector3 Vector3::GetAbsolute() const {
  return Vector3(fabsf(coords[0]), fabsf(coords[1]), fabsf(coords[2]));
}

Vector3 Vector3::EnforceMaximumDeviation(const Vector3& other, float max_deviation) const {
  Vector3 result = *this;
  Vector3 difference = other - *this;
  float distance = difference.GetLength();
  if (distance > max_deviation) {
    result += difference.GetNormalized() * (distance - max_deviation);
  }
  if (max_deviation == 0.0f) assert(other.GetDistance(result) < 0.001f);
  return result;
}

Vector3 Vector3::GetClamped2D(const Vector3& first_boundary, const Vector3& second_boundary) const {
  Vector3 result = *this;
  radian boundary_angle = second_boundary.GetSignedAngleFromXY(first_boundary);
  signed int direction = signSide(boundary_angle);
  radian angle_to_first = GetSignedAngleFromXY(first_boundary);
  radian angle_to_second = GetSignedAngleFromXY(second_boundary);
  if (signSide(angle_to_first) != direction || signSide(angle_to_second) == direction) {
    if (fabs(angle_to_first) < fabs(angle_to_second)) {
      result = first_boundary;
    } else {
      result = second_boundary;
    }
  }
  return result;
}

void Vector3::Extrapolate(const Vector3& velocity, unsigned long duration_milliseconds) {
  *this += velocity * (duration_milliseconds / 1000.0);
}

std::ostream& operator<<(std::ostream& os, const Vector3& vector) {
  os << vector.coords[0] << " " << vector.coords[1] << " " << vector.coords[2];
  return os;
}

}
