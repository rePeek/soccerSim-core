#pragma once

#include <cmath>
#include <iostream>

#include "football/foundation/math/quaternion.hpp"
#include "football/foundation/math/scalar.hpp"

namespace football::math {

class Vector3;
using Vector = Vector3;

class Vector3 {
 public:
  Vector3();
  Vector3(const Vector3& other) {
    coords[0] = other.coords[0];
    coords[1] = other.coords[1];
    coords[2] = other.coords[2];
  }
  Vector3(real xyz);
  Vector3(real x, real y, real z);
  static Vector3 FromRotatedNegativeZAxis(const Quaternion& orientation);

  void Mirror() {
    coords[0] = -coords[0];
    coords[1] = -coords[1];
  }
  void Set(real xyz);
  void Set(real x, real y, real z);
  void Set(const Vector3& vector);
  float GetCoord(int index) const;
  void SetCoord(int index, float value);

  bool operator==(const Vector3& other) const;
  bool operator!=(const Vector3& other) const;
  void operator=(real value);
  void operator=(const Vector3& other);
  Vector3 operator*(real factor) const;
  Vector3 operator*(const Vector3& factors) const;
  Vector3& operator*=(real factor);
  Vector3& operator*=(const Vector3& factors);
  Vector3 operator/(real divisor) const;
  Vector3 operator/(const Vector3& divisors) const;
  Vector3& operator/=(const Vector3& divisors);
  Vector3& operator+=(real value);
  Vector3& operator+=(const Vector3& other);
  Vector3& operator-=(const Vector3& other);
  Vector3 operator+(const Vector3& other) const;
  const Vector3& operator+() const;
  Vector3 operator+(real value) const;
  Vector3 operator-(const Vector3& other) const;
  Vector3 operator-() const;
  Vector3 operator-(real value) const;
  bool operator<(const Vector3& other) const;

  Vector3 GetCrossProduct(const Vector3& other) const;
  real GetDotProduct(const Vector3& other) const;
  void FastNormalize();
  void Normalize(const Vector3& fallback);
  void Normalize();
  void NormalizeTo(float length);
  void ClampLength(float max_length);
  Vector3 GetNormalized(const Vector3& fallback) const;
  Vector3 GetNormalized() const;
  Vector3 GetNormalizedTo(float length) const;
  Vector3 GetLengthClamped(float max_length) const;
  real GetDistance(const Vector3& other) const;
  real GetLength() const;
  real GetSquaredLength() const;
  radian GetAzimuthRadians() const;
  radian GetSignedAngleFromXY(const Vector3& reference) const;
  void Rotate(const Quaternion& quaternion);
  void Rotate2D(radian angle);
  Vector3 GetRotated2D(radian angle) const;
  Vector3 Get2D() const;
  bool Compare(const Vector3& other) const;
  Vector3 GetAbsolute() const;
  Vector3 EnforceMaximumDeviation(const Vector3& other, float max_deviation) const;
  Vector3 GetClamped2D(const Vector3& first_boundary, const Vector3& second_boundary) const;
  void Extrapolate(const Vector3& velocity, unsigned long duration_milliseconds);

  real coords[3];
};

std::ostream& operator<<(std::ostream& os, const Vector3& vector);

inline bool Vector3::operator==(const Vector3& other) const {
  return coords[0] == other.coords[0] && coords[1] == other.coords[1] &&
         coords[2] == other.coords[2];
}

inline bool Vector3::operator!=(const Vector3& other) const {
  return !(*this == other);
}

inline void Vector3::operator=(real value) {
  Set(value);
}

inline void Vector3::operator=(const Vector3& other) {
  coords[0] = other.coords[0];
  coords[1] = other.coords[1];
  coords[2] = other.coords[2];
}

inline Vector3 Vector3::operator*(real factor) const {
  return Vector3(coords[0] * factor, coords[1] * factor, coords[2] * factor);
}

inline Vector3 Vector3::operator*(const Vector3& factors) const {
  return Vector3(coords[0] * factors.coords[0], coords[1] * factors.coords[1],
                 coords[2] * factors.coords[2]);
}

inline Vector3& Vector3::operator*=(real factor) {
  coords[0] *= factor;
  coords[1] *= factor;
  coords[2] *= factor;
  return *this;
}

inline Vector3& Vector3::operator*=(const Vector3& factors) {
  coords[0] *= factors.coords[0];
  coords[1] *= factors.coords[1];
  coords[2] *= factors.coords[2];
  return *this;
}

inline Vector3 Vector3::operator/(real divisor) const {
  return Vector3(coords[0] / divisor, coords[1] / divisor, coords[2] / divisor);
}

inline Vector3 Vector3::operator/(const Vector3& divisors) const {
  return Vector3(coords[0] / divisors.coords[0], coords[1] / divisors.coords[1],
                 coords[2] / divisors.coords[2]);
}

inline Vector3& Vector3::operator/=(const Vector3& divisors) {
  coords[0] /= divisors.coords[0];
  coords[1] /= divisors.coords[1];
  coords[2] /= divisors.coords[2];
  return *this;
}

inline Vector3& Vector3::operator+=(real value) {
  coords[0] += value;
  coords[1] += value;
  coords[2] += value;
  return *this;
}

inline Vector3& Vector3::operator+=(const Vector3& other) {
  coords[0] += other.coords[0];
  coords[1] += other.coords[1];
  coords[2] += other.coords[2];
  return *this;
}

inline Vector3& Vector3::operator-=(const Vector3& other) {
  coords[0] -= other.coords[0];
  coords[1] -= other.coords[1];
  coords[2] -= other.coords[2];
  return *this;
}

inline Vector3 Vector3::operator+(const Vector3& other) const {
  return Vector3(coords[0] + other.coords[0], coords[1] + other.coords[1],
                 coords[2] + other.coords[2]);
}

inline const Vector3& Vector3::operator+() const {
  return *this;
}

inline Vector3 Vector3::operator+(real value) const {
  return Vector3(coords[0] + value, coords[1] + value, coords[2] + value);
}

inline Vector3 Vector3::operator-(const Vector3& other) const {
  return Vector3(coords[0] - other.coords[0], coords[1] - other.coords[1],
                 coords[2] - other.coords[2]);
}

inline Vector3 Vector3::operator-() const {
  return Vector3(-coords[0], -coords[1], -coords[2]);
}

inline Vector3 Vector3::operator-(real value) const {
  return Vector3(coords[0] - value, coords[1] - value, coords[2] - value);
}

inline bool Vector3::operator<(const Vector3& other) const {
  if (coords[0] == other.coords[0]) {
    if (coords[1] == other.coords[1]) return coords[2] < other.coords[2];
    return coords[1] < other.coords[1];
  }
  return coords[0] < other.coords[0];
}

inline real Vector3::GetLength() const {
  float length = sqrt(std::pow(coords[0], 2) + std::pow(coords[1], 2) +
                      std::pow(coords[2], 2));
  if (length < 0.000001) length = 0;
  return length;
}

inline real Vector3::GetSquaredLength() const {
  return coords[0] * coords[0] + coords[1] * coords[1] + coords[2] * coords[2];
}

inline radian Vector3::GetAzimuthRadians() const {
  real angle = std::atan2(coords[1], coords[0]);
  if (angle < 0) angle += 2 * pi;
  return angle;
}

inline radian Vector3::GetSignedAngleFromXY(const Vector3& reference) const {
  radian angle = -std::atan2(coords[0] * reference.coords[1] - coords[1] * reference.coords[0],
                            coords[0] * reference.coords[0] + coords[1] * reference.coords[1]);
  angle = ModulateIntoRange(-pi, pi, angle);
  return angle;
}

inline void Vector3::Rotate(const Quaternion& quaternion) {
  float cross_x = coords[2] * quaternion.elements[1] - coords[1] * quaternion.elements[2];
  float cross_y = coords[0] * quaternion.elements[2] - coords[2] * quaternion.elements[0];
  float cross_z = coords[1] * quaternion.elements[0] - coords[0] * quaternion.elements[1];
  float double_cross_x = cross_z * quaternion.elements[1] - cross_y * quaternion.elements[2];
  float double_cross_y = cross_x * quaternion.elements[2] - cross_z * quaternion.elements[0];
  float double_cross_z = cross_y * quaternion.elements[0] - cross_x * quaternion.elements[1];
  cross_x *= 2.0f * quaternion.elements[3];
  cross_y *= 2.0f * quaternion.elements[3];
  cross_z *= 2.0f * quaternion.elements[3];
  double_cross_x *= 2.0f;
  double_cross_y *= 2.0f;
  double_cross_z *= 2.0f;
  coords[0] += cross_x + double_cross_x;
  coords[1] += cross_y + double_cross_y;
  coords[2] += cross_z + double_cross_z;
}

inline void Vector3::Rotate2D(radian angle) {
  real x = (coords[0] * std::cos(angle)) - (coords[1] * std::sin(angle));
  real y = (coords[1] * std::cos(angle)) + (coords[0] * std::sin(angle));
  coords[0] = x;
  coords[1] = y;
}

inline Vector3 Vector3::GetRotated2D(radian angle) const {
  Vector3 result;
  real x = (coords[0] * std::cos(angle)) - (coords[1] * std::sin(angle));
  real y = (coords[1] * std::cos(angle)) + (coords[0] * std::sin(angle));
  result.coords[0] = x;
  result.coords[1] = y;
  result.coords[2] = coords[2];
  return result;
}

inline Vector3 Vector3::Get2D() const {
  Vector3 result(coords[0], coords[1], 0);
  return result;
}

}
