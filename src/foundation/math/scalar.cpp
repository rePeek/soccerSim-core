#include "football/foundation/math/scalar.hpp"

#include <cmath>


namespace football::math {

  unsigned int fastrandseed = 0;

  real clamp(const real value, const real min, const real max) {
    assert(max >= min);
    if (min > value) return min;
    if (max < value) return max;
    return value;
  }

  real NormalizedClamp(const real value, const real min, const real max) {
    assert(max > min);
    real banana = clamp(value, min, max);
    banana = (banana - min) / (max - min);
    return banana;
  }

  float dot_product(real v1[3], real v2[3]) {
    return (v1[0] * v2[0] + v1[1] * v2[1] + v1[2] * v2[2]);
  }

  void normalize(real v[3]) {
    real f = 1.0f / std::sqrt(dot_product(v, v));

    v[0] *= f;
    v[1] *= f;
    v[2] *= f;
  }

  signed int signSide(real n) {
    return n >= 0 ? 1 : -1;
  }

  bool is_odd(int n) {
    return n & 1;
  }


  radian ModulateIntoRange(real min, real max, radian value) {
    real step = max - min;
    real newValue = value;
    while (newValue < min) newValue += step;
    while (newValue > max) newValue -= step;
    return newValue;
  }
}
