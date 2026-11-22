#include "football/foundation/math/scalar.hpp"

#include <cmath>

namespace football::math {

real clamp(const real value, const real min, const real max) {
  assert(max >= min);
  if (min > value) return min;
  if (max < value) return max;
  return value;
}

real NormalizedClamp(const real value, const real min, const real max) {
  assert(max > min);
  real normalized_value = clamp(value, min, max);
  normalized_value = (normalized_value - min) / (max - min);
  return normalized_value;
}

float dot_product(real first[3], real second[3]) {
  return (first[0] * second[0] + first[1] * second[1] + first[2] * second[2]);
}

void normalize(real vector[3]) {
  real inverse_length = 1.0f / std::sqrt(dot_product(vector, vector));
  vector[0] *= inverse_length;
  vector[1] *= inverse_length;
  vector[2] *= inverse_length;
}

signed int signSide(real value) {
  return value >= 0 ? 1 : -1;
}

bool is_odd(int value) {
  return value & 1;
}

radian ModulateIntoRange(real min, real max, radian value) {
  real range_width = max - min;
  real wrapped_value = value;
  while (wrapped_value < min) wrapped_value += range_width;
  while (wrapped_value > max) wrapped_value -= range_width;
  return wrapped_value;
}

}
