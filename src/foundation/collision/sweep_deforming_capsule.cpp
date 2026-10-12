#include "sweep_deforming_capsule.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace football::collision::detail {

using football::geometry::Capsule;
using football::geometry::ColliderMotion;

namespace {

// Compute roots/geometry in double; public state/evidence remains float. Working
// storage is fixed-size: a quartic has at most four roots, two caps add four.
struct V {
  double x, y, z;
  V operator+(V b) const { return {x+b.x, y+b.y, z+b.z}; }
  V operator-(V b) const { return {x-b.x, y-b.y, z-b.z}; }
  V operator*(double s) const { return {x*s, y*s, z*s}; }
  double dot(V b) const { return x*b.x + y*b.y + z*b.z; }
  V cross(V b) const { return {y*b.z-z*b.y, z*b.x-x*b.z, x*b.y-y*b.x}; }
  double length() const { return std::sqrt(dot(*this)); }
};

V Vec(const football::math::Vector3& v) { return {v.coords[0], v.coords[1], v.coords[2]}; }
football::math::Vector3 Float(V v) { return {float(v.x), float(v.y), float(v.z)}; }

using Polynomial = std::array<double, 5>;  // ascending powers

struct Roots {
  std::array<double, 4> values{};
  int count = 0;
  void Add(double t) {
    if (t < 0 || t > 1) return;
    int at = 0;
    while (at < count && values[at] < t) ++at;
    if (at < count && values[at] == t) return;
    if (count == 4) throw std::logic_error("polynomial root isolation exceeded degree");
    for (int i = count; i > at; --i) values[i] = values[i-1];
    values[at] = t; ++count;
  }
};

double Evaluate(const Polynomial& p, int degree, double t) {
  double f = p[degree];
  for (int i = degree-1; i >= 0; --i) f = f*t + p[i];
  return f;
}

// Derivative roots partition [0,1] into monotone intervals. Sign-changing roots
// are bracketed and bisected (64 fixed iterations), not found by temporal point
// sampling or a speed-dependent subdivision cap. Even-multiplicity tangencies
// have no incoming normal speed; exact stationary zeros are retained as well.
Roots Isolate(const Polynomial& p, int degree) {
  while (degree > 0 && p[degree] == 0) --degree;
  Roots out;
  if (degree == 0) return out;
  if (degree == 1) { out.Add(-p[0]/p[1]); return out; }
  Polynomial derivative{};
  for (int i = 1; i <= degree; ++i) derivative[i-1] = p[i]*i;
  const auto critical = Isolate(derivative, degree-1);
  std::array<double, 6> bounds{};
  int size = 1; bounds[0] = 0;
  for (int i = 0; i < critical.count; ++i)
    if (critical.values[i] > 0 && critical.values[i] < 1) bounds[size++] = critical.values[i];
  bounds[size++] = 1;
  for (int i = 0; i < size; ++i)
    if (Evaluate(p, degree, bounds[i]) == 0) out.Add(bounds[i]);
  for (int i = 1; i < size; ++i) {
    double lo = bounds[i-1], hi = bounds[i];
    double flo = Evaluate(p, degree, lo), fhi = Evaluate(p, degree, hi);
    if (flo == 0 || fhi == 0 || (flo < 0) == (fhi < 0)) continue;
    for (int iteration = 0; iteration < 64; ++iteration) {
      const double mid = (lo+hi)*.5;
      const double f = Evaluate(p, degree, mid);
      if (f == 0) { lo = hi = mid; break; }
      if ((f < 0) == (flo < 0)) { lo = mid; flo = f; }
      else hi = mid;
    }
    out.Add((lo+hi)*.5);
  }
  return out;
}

Polynomial CapPolynomial(V q, V v, double radius, double dr) {
  return {q.dot(q)-radius*radius, 2*(q.dot(v)-radius*dr), v.dot(v)-dr*dr, 0, 0};
}

bool Finite(V v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

}  // namespace

std::optional<CollisionHit> SweepDeformingCapsule(const MovingSphere& sphere,
                                                  const ColliderMotion& collider,
                                                  const Capsule& start,
                                                  const Capsule& end, float dt) {
  const football::math::Vector3 ball_position = sphere.shape.center;
  const football::math::Vector3 ball_velocity = sphere.velocity;
  const float ball_radius = sphere.shape.radius;

  const V p = Vec(ball_position), velocity = Vec(ball_velocity);
  const V a = Vec(start.a), b = Vec(start.b), da = Vec(end.a)-a, db = Vec(end.b)-b;
  if (!(dt > 0) || !std::isfinite(dt) || !(ball_radius >= 0) || !std::isfinite(ball_radius) ||
      !Finite(p) || !Finite(velocity) || !Finite(a) || !Finite(b) || !Finite(da) || !Finite(db) ||
      !(start.radius >= 0) || !(end.radius >= 0) ||
      !std::isfinite(start.radius) || !std::isfinite(end.radius))
    throw std::invalid_argument("non-finite or negative capsule sweep geometry/time");
  const V disp = velocity*dt;
  const double radius = double(ball_radius)+start.radius, dr = double(end.radius)-start.radius;
  // Conservative swept AABB: endpoint/radius interpolation cannot leave this
  // box. This culls distant rotating anatomy without roots or heap allocation.
  const double extent = double(ball_radius)+std::max(start.radius, end.radius);
  for (int axis = 0; axis < 3; ++axis) {
    const double p0 = ball_position.coords[axis], p1 = p0+ball_velocity.coords[axis]*double(dt);
    const double min_c = std::min({double(start.a.coords[axis]), double(start.b.coords[axis]),
                                  double(end.a.coords[axis]), double(end.b.coords[axis])});
    const double max_c = std::max({double(start.a.coords[axis]), double(start.b.coords[axis]),
                                  double(end.a.coords[axis]), double(end.b.coords[axis])});
    if (std::max(p0,p1) < min_c-extent || std::min(p0,p1) > max_c+extent) return std::nullopt;
  }
  const V d0 = b-a, dv = db-da, q0 = p-a, qv = disp-da;
  const auto at = [&](double t, bool overlap) -> std::optional<CollisionHit> {
    const V axis = d0+dv*t, q = q0+qv*t;
    const double len2 = axis.dot(axis);
    const double s = len2 > 0 ? std::clamp(q.dot(axis)/len2, 0.0, 1.0) : 0;
    const V radial = q-axis*s;
    const double distance = radial.length(), r = start.radius+dr*t;
    if (!overlap && distance > ball_radius+r+1e-6) return std::nullopt;
    V fallback = axis.cross({0,0,1});
    if (fallback.dot(fallback) == 0) fallback = axis.cross({1,0,0});
    if (fallback.dot(fallback) == 0) fallback = {0,0,1};
    const V n = distance > 0 ? radial*(1/distance) : fallback*(1/fallback.length());
    // Minimal-twist transport of the axis frame. Endpoint blending describes
    // axis translation/stretch; omega x (r*n) supplies rotational tangential
    // surface motion. Radius change contributes normal dilation. No axial spin
    // can be inferred from two capsule endpoints, so that component is zero.
    const V omega = len2 > 0 ? axis.cross(dv)*(1/(len2*dt)) : V{0,0,0};
    const V surface_velocity = (da*(1-s)+db*s)*(1.0/dt) + omega.cross(n*r) + n*(dr/dt);
    const V relative = velocity-surface_velocity;
    if (!overlap && relative.dot(n) >= 0) return std::nullopt;  // never choose an exit root
    CollisionHit hit;
    hit.collider = collider.id;
    hit.toi = float(t);
    hit.center = Float(p+disp*t);
    hit.normal = Float(n);
    hit.surface_velocity = Float(surface_velocity);
    return hit;
  };
  const double s0 = d0.dot(d0) > 0 ? std::clamp(q0.dot(d0)/d0.dot(d0), 0.0, 1.0) : 0;
  if ((q0-d0*s0).length() < radius-1e-6) return at(0, true);

  // Distance to the infinite moving axis: |q x d|^2 - R(t)^2 |d|^2.
  // q,d,R are affine, so this is quartic. The finite-segment feature test in
  // at() rejects roots outside the capsule; two cap quadratics complete it.
  const V c0 = q0.cross(d0), c1 = q0.cross(dv)+qv.cross(d0), c2 = qv.cross(dv);
  Polynomial interior{c0.dot(c0), 2*c0.dot(c1), c1.dot(c1)+2*c0.dot(c2),
                     2*c1.dot(c2), c2.dot(c2)};
  const std::array<double,3> rr{radius*radius, 2*radius*dr, dr*dr};
  const std::array<double,3> dd{d0.dot(d0), 2*d0.dot(dv), dv.dot(dv)};
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) interior[i+j] -= rr[i]*dd[j];
  std::optional<CollisionHit> best;
  const auto consider = [&](const Roots& roots) {
    for (int i = 0; i < roots.count; ++i) {
      const auto contact = at(roots.values[i], false);
      if (contact && (!best || contact->toi < best->toi)) best = contact;
    }
  };
  consider(Isolate(interior, 4));
  consider(Isolate(CapPolynomial(q0, qv, radius, dr), 2));
  consider(Isolate(CapPolynomial(p-b, disp-db, radius, dr), 2));
  return best;
}

}  // namespace football::collision::detail
