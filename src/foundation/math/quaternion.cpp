#include "football/foundation/math/quaternion.hpp"

#include <cmath>

#include "football/foundation/math/scalar.hpp"
#include "football/foundation/math/vector3.hpp"

namespace football::math {

Quaternion::Quaternion(real x, real y, real z, real w) {
  elements[0] = x;
  elements[1] = y;
  elements[2] = z;
  elements[3] = w;
}

Quaternion::Quaternion(const real values[4]) {
  elements[0] = values[0];
  elements[1] = values[1];
  elements[2] = values[2];
  elements[3] = values[3];
}

void Quaternion::Set(real x, real y, real z, real w) {
  elements[0] = x;
  elements[1] = y;
  elements[2] = z;
  elements[3] = w;
}

void Quaternion::Set(const Quaternion& quaternion) {
  elements[0] = quaternion.elements[0];
  elements[1] = quaternion.elements[1];
  elements[2] = quaternion.elements[2];
  elements[3] = quaternion.elements[3];
}

bool Quaternion::operator!=(const Quaternion& other) const {
  return other.elements[0] != elements[0] || other.elements[1] != elements[1] ||
         other.elements[2] != elements[2] || other.elements[3] != elements[3];
}

const Quaternion Quaternion::operator*(float factor) const {
  return Quaternion(elements[0] * factor, elements[1] * factor,
                    elements[2] * factor, elements[3] * factor);
}

Vector3 Quaternion::operator*(const Vector3& vector) const {
  Vector3 cross, double_cross;
  Vector3 imaginary_part(elements[0], elements[1], elements[2]);
  cross = imaginary_part.GetCrossProduct(vector);
  double_cross = imaginary_part.GetCrossProduct(cross);
  cross *= (2.0f * elements[3]);
  double_cross *= 2.0f;
  return vector + cross + double_cross;
}

Quaternion Quaternion::operator*(const Quaternion& other) const {
  Quaternion result;
  result.elements[0] = elements[3] * other.elements[0] + elements[0] * other.elements[3] +
                       elements[1] * other.elements[2] - elements[2] * other.elements[1];
  result.elements[1] = elements[3] * other.elements[1] + elements[1] * other.elements[3] +
                       elements[2] * other.elements[0] - elements[0] * other.elements[2];
  result.elements[2] = elements[3] * other.elements[2] + elements[2] * other.elements[3] +
                       elements[0] * other.elements[1] - elements[1] * other.elements[0];
  result.elements[3] = elements[3] * other.elements[3] - elements[0] * other.elements[0] -
                       elements[1] * other.elements[1] - elements[2] * other.elements[2];
  return result;
}

Quaternion Quaternion::operator+(const Quaternion& other) const {
  return Quaternion(elements[0] + other.elements[0], elements[1] + other.elements[1],
                    elements[2] + other.elements[2], elements[3] + other.elements[3]);
}

Quaternion Quaternion::operator-(const Quaternion& other) const {
  return Quaternion(elements[0] - other.elements[0], elements[1] - other.elements[1],
                    elements[2] - other.elements[2], elements[3] - other.elements[3]);
}

Quaternion Quaternion::operator-() const {
  return Quaternion(-elements[0], -elements[1], -elements[2], -elements[3]);
}

Quaternion Quaternion::GetInverse() const {
  real magnitude = GetMagnitude();
  if (magnitude < 0.000001f) {
    return QUATERNION_IDENTITY;
  }
  real inverse_magnitude = 1.0 / magnitude;
  return Quaternion(-elements[0] * inverse_magnitude, -elements[1] * inverse_magnitude,
                    -elements[2] * inverse_magnitude, elements[3] * inverse_magnitude);
}

void Quaternion::GetEulerAnglesXYZ(real& x_radians, real& y_radians, real& z_radians) const {
  constexpr int x_index = 0;
  constexpr int z_index = 2;
  constexpr int y_index = 1;
  float singularity = elements[x_index] * elements[z_index] + elements[y_index] * elements[3];
  if (singularity > 0.49999 || singularity < -0.49999) {
    if (singularity > 0) {
      z_radians = 2 * std::atan2(elements[x_index], elements[y_index]);
      y_radians = pi * 0.5;
    }
    if (singularity < 0) {
      z_radians = -2 * std::atan2(elements[x_index], elements[y_index]);
      y_radians = -pi * 0.5;
    }
    x_radians = 0;
    return;
  }
  real squared_x = elements[x_index] * elements[x_index];
  real squared_z = elements[z_index] * elements[z_index];
  real squared_y = elements[y_index] * elements[y_index];
  z_radians = std::atan2(
      2 * elements[z_index] * elements[3] - 2 * elements[x_index] * elements[y_index],
      1 - 2 * squared_z - 2 * squared_y);
  y_radians = std::asin(2 * elements[x_index] * elements[z_index] +
                        2 * elements[y_index] * elements[3]);
  x_radians = std::atan2(
      2 * elements[x_index] * elements[3] - 2 * elements[z_index] * elements[y_index],
      1 - 2 * squared_x - 2 * squared_y);
}

void Quaternion::SetEulerAnglesYZX(real x_radians, real y_radians, real z_radians) {
  float cos_half_y = cos(y_radians / 2.0);
  float sin_half_y = sin(y_radians / 2.0);
  float cos_half_z = cos(z_radians / 2.0);
  float sin_half_z = sin(z_radians / 2.0);
  float cos_half_x = cos(x_radians / 2.0);
  float sin_half_x = sin(x_radians / 2.0);
  float cos_y_cos_z = cos_half_y * cos_half_z;
  float sin_y_sin_z = sin_half_y * sin_half_z;
  elements[3] = cos_y_cos_z * cos_half_x - sin_y_sin_z * sin_half_x;
  elements[0] = cos_y_cos_z * sin_half_x + sin_y_sin_z * cos_half_x;
  elements[1] = sin_half_y * cos_half_z * cos_half_x + cos_half_y * sin_half_z * sin_half_x;
  elements[2] = cos_half_y * sin_half_z * cos_half_x - sin_half_y * cos_half_z * sin_half_x;
}

void Quaternion::GetAngleAxis(radian& angle, Vector3& axis) const {
  angle = 2.0f * std::acos(elements[3]);
  double sin_half_angle = std::sqrt(1.0f - elements[3] * elements[3]);
  if (sin_half_angle < 0.000001f) {
    axis.coords[0] = elements[0];
    axis.coords[1] = elements[1];
    axis.coords[2] = elements[2];
  } else {
    axis.coords[0] = elements[0] / sin_half_angle;
    axis.coords[1] = elements[1] / sin_half_angle;
    axis.coords[2] = elements[2] / sin_half_angle;
  }
}

void Quaternion::SetAngleAxis(const radian& angle, const Vector3& unit_axis) {
  radian half_angle = 0.5f * angle;
  real sin_half_angle = sinf(half_angle);
  elements[0] = sin_half_angle * unit_axis.coords[0];
  elements[1] = sin_half_angle * unit_axis.coords[1];
  elements[2] = sin_half_angle * unit_axis.coords[2];
  elements[3] = cosf(half_angle);
}

void Quaternion::Conjugate() {
  elements[0] = -elements[0];
  elements[1] = -elements[1];
  elements[2] = -elements[2];
}

Quaternion Quaternion::GetConjugated() const {
  return Quaternion(-elements[0], -elements[1], -elements[2], elements[3]);
}

void Quaternion::Scale(real factor) {
  for (int index = 0; index < 4; index++) {
    elements[index] *= factor;
  }
}

real Quaternion::GetMagnitude() const {
  if (elements[0] == 0.0f && elements[1] == 0.0f &&
      elements[2] == 0.0f && elements[3] == 0.0f) return 0.0f;
  real magnitude = std::sqrt(elements[0] * elements[0] + elements[1] * elements[1] +
                             elements[2] * elements[2] + elements[3] * elements[3]);
  if (magnitude < 0.000001) return 0.0f;
  return magnitude;
}

void Quaternion::Normalize() {
  double squared_magnitude = elements[0] * elements[0] + elements[1] * elements[1] +
                             elements[2] * elements[2] + elements[3] * elements[3];
  if (squared_magnitude < 0.000001f) {
    Set(QUATERNION_IDENTITY[0], QUATERNION_IDENTITY[1],
        QUATERNION_IDENTITY[2], QUATERNION_IDENTITY[3]);
  } else {
    if (std::fabs(1.0 - squared_magnitude) < 2.107342e-08) {
      Scale(2.0 / (1.0 + squared_magnitude));
    } else {
      Scale(1.0 / sqrt(squared_magnitude));
    }
  }
}

Quaternion Quaternion::GetNormalized() const {
  Quaternion normalized(*this);
  normalized.Normalize();
  return normalized;
}

float Quaternion::GetDotProduct(const Quaternion& other) const {
  return (elements[0] * other.elements[0] + elements[1] * other.elements[1] +
          elements[2] * other.elements[2] + elements[3] * other.elements[3]);
}

Quaternion Quaternion::GetLerped(float fraction, const Quaternion& target) const {
  return (*this * (1 - fraction) + target * fraction).GetNormalized();
}

Quaternion Quaternion::GetSlerped(float fraction, const Quaternion& target) const {
  Quaternion interpolated;
  Quaternion same_hemisphere_target = target;
  double cos_half_angle = elements[3] * same_hemisphere_target.elements[3] +
                          elements[0] * same_hemisphere_target.elements[0] +
                          elements[1] * same_hemisphere_target.elements[1] +
                          elements[2] * same_hemisphere_target.elements[2];
  if (cos_half_angle < 0) {
    same_hemisphere_target = -same_hemisphere_target;
    cos_half_angle = -cos_half_angle;
  }
  if (fabs(cos_half_angle) >= 1.0) {
    interpolated.elements[3] = elements[3];
    interpolated.elements[0] = elements[0];
    interpolated.elements[1] = elements[1];
    interpolated.elements[2] = elements[2];
    return interpolated;
  }
  double half_angle = acos(cos_half_angle);
  double sin_half_angle = sqrt(1.0 - cos_half_angle * cos_half_angle);
  if (fabs(sin_half_angle) < 0.000001f) {
    interpolated.elements[3] = (elements[3] * 0.5 + same_hemisphere_target.elements[3] * 0.5);
    interpolated.elements[0] = (elements[0] * 0.5 + same_hemisphere_target.elements[0] * 0.5);
    interpolated.elements[1] = (elements[1] * 0.5 + same_hemisphere_target.elements[1] * 0.5);
    interpolated.elements[2] = (elements[2] * 0.5 + same_hemisphere_target.elements[2] * 0.5);
    return interpolated;
  }
  double source_weight = sin((1 - fraction) * half_angle) / sin_half_angle;
  double target_weight = sin(fraction * half_angle) / sin_half_angle;
  interpolated.elements[3] =
      (elements[3] * source_weight + same_hemisphere_target.elements[3] * target_weight);
  interpolated.elements[0] =
      (elements[0] * source_weight + same_hemisphere_target.elements[0] * target_weight);
  interpolated.elements[1] =
      (elements[1] * source_weight + same_hemisphere_target.elements[1] * target_weight);
  interpolated.elements[2] =
      (elements[2] * source_weight + same_hemisphere_target.elements[2] * target_weight);
  return interpolated;
}

Quaternion Quaternion::GetRotationTo(const Quaternion& target) const {
  return target * this->GetInverse();
}

Quaternion Quaternion::GetRotationMultipliedBy(float factor) const {
  Quaternion result;
  Vector3 axis;
  radian angle;
  GetAngleAxis(angle, axis);
  if (angle > pi) angle -= 2.0f * pi;
  angle = std::fmod(angle * factor, 2.0f * pi);
  result.SetAngleAxis(angle, axis);
  return result.GetNormalized();
}

float Quaternion::MatchHemisphere(const Quaternion& reference) {
  float dot = GetDotProduct(reference);
  if (dot < 0) {
    dot = -dot;
    *this = -*this;
  }
  return dot;
}

std::ostream& operator<<(std::ostream& os, const Quaternion& quaternion) {
  os << quaternion.elements[0] << " " << quaternion.elements[1] << " "
     << quaternion.elements[2] << " " << quaternion.elements[3];
  return os;
}

Quaternion IntegrateOrientation(const Quaternion& orientation,
                                const Vector3& angular_velocity, real dt) {
  const Quaternion angular_velocity_quaternion(
      angular_velocity.coords[0], angular_velocity.coords[1], angular_velocity.coords[2], 0.0f);
  return (orientation + angular_velocity_quaternion * orientation * (0.5f * dt)).GetNormalized();
}

}
