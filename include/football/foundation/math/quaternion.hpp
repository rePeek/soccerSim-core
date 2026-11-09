#pragma once


#include <cmath>
#include <iostream>

#include "football/foundation/math/scalar.hpp"

namespace football::math {

  class Vector3;

  static real QUATERNION_IDENTITY[4] = { 0, 0, 0, 1 };

  class Quaternion {

    public:
      inline Quaternion() {
        elements[0] = 0;
        elements[1] = 0;
        elements[2] = 0;
        elements[3] = 1;
      }
      Quaternion(real x, real y, real z, real w);
      Quaternion(real values[4]);

      void Set(real x, real y, real z, real w);
      void Set(const Quaternion &quat);

      // ----- operator overloading
      bool operator != (const Quaternion &fac) const;
      const Quaternion operator * (float scale) const;
      Vector3 operator * (const Vector3 &fac) const;

      Quaternion operator * (const Quaternion &fac) const;
      Quaternion operator + (const Quaternion &q2) const;
      Quaternion operator - (const Quaternion &q2) const;
      Quaternion operator - () const;

      // ----- mathematics
      Quaternion GetInverse() const;
      void GetAngles(real &X, real &Y, real &Z) const;
      void SetAngles(real X, real Y, real Z);
      void GetAngleAxis(radian &rfangle, Vector3 &rkaxis) const;
      void SetAngleAxis(const radian &rfangle, const Vector3 &rkaxis);
      void conjugate();
      Quaternion conjugate_get() const;
      void scale(const real fac);
      real GetMagnitude() const;
      void Normalize();
      Quaternion GetNormalized() const;
      float GetDotProduct(const Quaternion &subject) const;
      Quaternion GetLerped(float bias, const Quaternion &to) const; // bias > 1 can extrapolate small angles in a hacky, incorrect way *edit: is that so? i'm not so sure
      Quaternion GetSlerped(float bias, const Quaternion &to) const;
      Quaternion GetRotationTo(const Quaternion &to) const;
      Quaternion GetRotationMultipliedBy(float factor) const;
      radian GetRotationAngle(const Quaternion &to) const {
        return 2.0f * std::acos(clamp(this->GetDotProduct(to), -1.0f, 1.0f));
      }
      float MakeSameNeighborhood(const Quaternion &src); // returns dot product as added bonus! ;)

      real elements[4];
  };

  std::ostream& operator<<(std::ostream& os, const Quaternion& v);

}
