#pragma once

#include <cassert>
#include <cmath>
#include <iostream>

namespace football::math {

using real = float;

real clamp(real value, real min, real max);
real NormalizedClamp(real value, real min, real max);

constexpr real pi = 3.1415926535897932384626433832795028841972f;
constexpr real EPSILON = 0.000001f;

class radian {
 public:
  radian() {}
  radian(float angle) : angle_(angle) {}

  std::ostream& operator<<(std::ostream& os) {
    os << angle_ << " " << mirrored_;
    return os;
  }
  radian& operator+=(radian other) {
    angle_ += other.angle_;
    mirrored_ ^= other.mirrored_;
    return *this;
  }
  radian& operator-=(radian other) {
    angle_ -= other.angle_;
    mirrored_ ^= other.mirrored_;
    return *this;
  }
  radian& operator/=(radian other) {
    *this = radian(real(*this) / real(other));
    return *this;
  }
  radian& operator*=(radian other) {
    *this = radian(real(*this) * real(other));
    return *this;
  }
  operator real() const {
    if (mirrored_) return angle_ - pi;
    return angle_;
  }
  void Mirror() {
    mirrored_ = !mirrored_;
  }

 private:
  float angle_ = 0.0f;
  bool mirrored_ = false;
};

void normalize(real vector[3]);
signed int signSide(real value);
bool is_odd(int value);

inline float curve(float source, float bias = 1.0f) {
  return (std::sin((source - 0.5f) * pi) * 0.5f + 0.5f) * bias +
         source * (1.0f - bias);
}

radian ModulateIntoRange(real min, real max, radian value);

}
