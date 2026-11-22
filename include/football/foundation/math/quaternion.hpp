#pragma once

#include <cmath>
#include <iostream>

#include "football/foundation/math/scalar.hpp"

namespace football::math {

class Vector3;

inline constexpr real QUATERNION_IDENTITY[4] = {0, 0, 0, 1};

class Quaternion {
 public:
  Quaternion() {
    elements[0] = 0;
    elements[1] = 0;
    elements[2] = 0;
    elements[3] = 1;
  }
  Quaternion(real x, real y, real z, real w);
  Quaternion(const real values[4]);

  void Set(real x, real y, real z, real w);
  void Set(const Quaternion& quaternion);

  bool operator==(const Quaternion& other) const = default;
  bool operator!=(const Quaternion& other) const;
  const Quaternion operator*(float factor) const;
  Vector3 operator*(const Vector3& vector) const;
  Quaternion operator*(const Quaternion& other) const;
  Quaternion operator+(const Quaternion& other) const;
  Quaternion operator-(const Quaternion& other) const;
  Quaternion operator-() const;

  Quaternion GetInverse() const;
  void GetEulerAnglesXYZ(real& x_radians, real& y_radians, real& z_radians) const;
  void SetEulerAnglesYZX(real x_radians, real y_radians, real z_radians);
  void GetAngleAxis(radian& angle, Vector3& axis) const;
  void SetAngleAxis(const radian& angle, const Vector3& unit_axis);
  void Conjugate();
  Quaternion GetConjugated() const;
  void Scale(real factor);
  real GetMagnitude() const;
  void Normalize();
  Quaternion GetNormalized() const;
  float GetDotProduct(const Quaternion& other) const;
  Quaternion GetLerped(float fraction, const Quaternion& target) const;
  Quaternion GetSlerped(float fraction, const Quaternion& target) const;
  Quaternion GetRotationTo(const Quaternion& target) const;
  Quaternion GetRotationMultipliedBy(float factor) const;
  radian GetRotationAngle(const Quaternion& target) const {
    return 2.0f * std::acos(clamp(this->GetDotProduct(target), -1.0f, 1.0f));
  }
  float MatchHemisphere(const Quaternion& reference);

  real elements[4];
};

std::ostream& operator<<(std::ostream& os, const Quaternion& quaternion);

Quaternion IntegrateOrientation(const Quaternion& orientation,
                                const Vector3& angular_velocity, real dt);

}
