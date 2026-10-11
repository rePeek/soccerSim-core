#include <limits>
#include <algorithm>
#include <array>
#include <cmath>
#include <catch2/catch_test_macros.hpp>
#include "football/ball/ball.hpp"
#include "football/ball/ball_response.hpp"

using namespace football::ball;
using blunted::Vector3;
namespace {
constexpr float dt = .01f;
BallState At(Vector3 p, Vector3 v = Vector3(0)) { return {p, v, Vector3(0), blunted::Quaternion{}}; }
ColliderMotion Turn(ColliderId id = 42) {
  return {id, Capsule{{-1,0,1},{1,0,1},.03f}, Capsule{{0,-1,1},{0,1,1},.03f}, {.35f,.45f}};
}
Capsule ShapeAt(const ColliderMotion& c, float t) {
  const auto& a = std::get<Capsule>(c.start); const auto& b = std::get<Capsule>(c.end);
  return {a.a+(b.a-a.a)*t, a.b+(b.b-a.b)*t, a.radius+(b.radius-a.radius)*t};
}
float Gap(const BallState& ball, const ColliderMotion& c, float t, float radius) {
  const auto shape = ShapeAt(c,t); const auto axis = shape.b-shape.a;
  const auto p = ball.position+ball.velocity*(dt*t);
  const float len2 = axis.GetDotProduct(axis);
  const float s = len2 > 0 ? std::clamp((p-shape.a).GetDotProduct(axis)/len2,0.f,1.f) : 0.f;
  return (p-shape.a-axis*s).GetLength()-shape.radius-radius;
}
BallDynamics Free() { BallDynamics d; d.gravity=0; d.quadratic_resistance=0; d.ground_deceleration=0; return d; }
ColliderMotion Mirror(ColliderMotion c) {
  for (auto* shape : {&c.start,&c.end}) std::visit([](auto& s) {
    using T = std::decay_t<decltype(s)>;
    if constexpr (std::is_same_v<T,Capsule>) { s.a.Mirror(); s.b.Mirror(); }
    else if constexpr (std::is_same_v<T,Sphere>) s.center.Mirror();
    else { s.point.Mirror(); s.normal.Mirror(); }
  }, *shape);
  return c;
}
}
TEST_CASE("rotating capsule catches a stationary ball between clear endpoints", "[ball][deforming-ccd]") {
  const auto c = Turn(); const auto ball = At({.35f,.35f,1});
  REQUIRE(Gap(ball,c,0,.11f) > 0); REQUIRE(Gap(ball,c,1,.11f) > 0);
  REQUIRE(Gap(ball,c,.5f,.11f) < 0);
  const auto hit = SweepBall(ball,c,dt,.11f); REQUIRE(hit.has_value());
  REQUIRE(hit->toi > 0); REQUIRE(hit->toi < .5f);
  REQUIRE(std::fabs(Gap(ball,c,hit->toi,.11f)) < 2e-6f);
  REQUIRE(hit->relative_velocity.GetDotProduct(hit->normal) < 0);
  // Independently reconstruct the minimal-twist material point velocity.
  const auto shape = ShapeAt(c,hit->toi); const auto axis = shape.b-shape.a;
  const auto& a = std::get<Capsule>(c.start); const auto& b = std::get<Capsule>(c.end);
  const auto va = (b.a-a.a)/dt, vb = (b.b-a.b)/dt;
  const float s = (hit->point-shape.a).GetDotProduct(axis)/axis.GetDotProduct(axis);
  const auto omega = axis.GetCrossProduct(vb-va)/axis.GetDotProduct(axis);
  const auto expected = va*(1-s)+vb*s+omega.GetCrossProduct(hit->normal*shape.radius);
  REQUIRE((expected-hit->surface_velocity).GetLength() < 1e-3f);
  REQUIRE((va-hit->surface_velocity).GetLength() > 50);
  // Distance derivative fixes normal speed; rotation adds tangential motion too.
  const float h = 1e-4f;
  const float closing = (Gap(ball,c,hit->toi+h,.11f)-Gap(ball,c,hit->toi-h,.11f))/(2*h*dt);
  REQUIRE(std::fabs(closing-hit->relative_velocity.GetDotProduct(hit->normal)) < .05f);
  const football::model::BallConfig config;
  const std::array world{c};
  const auto result = AdvanceBallTick(ball,world,dt,config,Free());
  REQUIRE(result.contacts.size() == 1); REQUIRE(result.contacts[0].normal_impulse > 0);
  auto impact = ball; impact.position = hit->point;
  const auto correct = ResolveContactResponse(impact,*hit,c.material,hit->surface_velocity,config);
  const auto wrong = ResolveContactResponse(impact,*hit,c.material,va,config);
  REQUIRE((result.state.velocity-correct.state.velocity).GetLength() < 1e-4f);
  REQUIRE((result.state.angular_velocity-correct.state.angular_velocity).GetLength() < 1e-4f);
  REQUIRE((result.state.velocity-wrong.state.velocity).GetLength() > 10);
  REQUIRE(result.state.angular_velocity.GetLength() > 1);
  // Full Ball API still commits this same passive response through one Step.
  Ball owner{football::model::Pitch{}}; owner.Reset(ball);
  const auto before = owner.state(); const auto preview = owner.Predict(BallTickInput{world,{}});
  REQUIRE(owner.state().position == before.position);
  REQUIRE(owner.state().velocity == before.velocity);
  const auto commit = owner.Step(BallTickInput{world,{}});
  REQUIRE(commit.state.position == preview.state.position);
  REQUIRE(commit.state.velocity == preview.state.velocity);
  REQUIRE(commit.state.angular_velocity == preview.state.angular_velocity);
  REQUIRE(commit.contacts.front().surface_velocity == preview.contacts.front().surface_velocity);
}
TEST_CASE("stretching capsule and radius dilation use local surface motion", "[ball][deforming-ccd]") {
  ColliderMotion c{42,Capsule{{0,-1,1},{0,-.5f,1},.04f},Capsule{{0,-1,1},{0,1,1},.04f},{.35f,.45f}};
  const auto cap = SweepBall(At({0,0,1}),c,dt,.11f); REQUIRE(cap.has_value());
  REQUIRE(std::fabs(cap->toi-(.5f-.15f)/1.5f) < 1e-6f);
  REQUIRE((cap->surface_velocity-Vector3(0,150,0)).GetLength() < 1e-4f);
  const auto slower = SweepBall(At({0,0,1}),c,dt*2,.11f); REQUIRE(slower.has_value());
  REQUIRE(slower->toi == cap->toi);
  REQUIRE((slower->surface_velocity-cap->surface_velocity*.5f).GetLength() < 1e-4f);
  c = {42,Capsule{{0,0,1},{0,0,2},.05f},Capsule{{0,0,1},{0,0,2},.4f},{}};
  const auto dilation = SweepBall(At({.3f,0,1.5f}),c,dt,.11f); REQUIRE(dilation.has_value());
  REQUIRE(std::fabs(dilation->toi-.4f) < 1e-6f);
  REQUIRE((dilation->surface_velocity-Vector3(35,0,0)).GetLength() < 1e-4f);
  c.start = Sphere{{0,0,1.5f},.05f}; c.end = Sphere{{0,0,1.5f},.4f};
  const auto sphere = SweepBall(At({.3f,0,1.5f}),c,dt,.11f); REQUIRE(sphere.has_value());
  REQUIRE(sphere->toi == dilation->toi); REQUIRE(sphere->surface_velocity == dilation->surface_velocity);
}
TEST_CASE("deforming capsule rejects exit and infinite-line roots including collapsed axes", "[ball][deforming-ccd]") {
  // The axis collapses at t=1/3. Its quartic zero there is NOT a ball contact.
  ColliderMotion c{42,Capsule{{-1,0,1},{1,0,1},.04f},Capsule{{2,0,1},{-2,0,1},.04f},{}};
  const auto hit = SweepBall(At({1.3f,0,1}),c,dt,.11f); REQUIRE(hit.has_value());
  REQUIRE(std::fabs(hit->toi-(1.3f-.15f+1)/3) < 1e-6f);
  REQUIRE(hit->surface_velocity.coords[0] > 299);
  REQUIRE_FALSE(SweepBall(At({1.3f,.151f,1}),c,dt,.11f));
  // Initially receding normal motion cannot globally reject a later entry.
  c = Turn(); c.end = Capsule{{.70710678f,-.70710678f,1},{-.70710678f,.70710678f,1},.03f};
  const auto late = SweepBall(At({.35f,-.35f,1}),c,dt,.11f); REQUIRE(late.has_value());
  REQUIRE(late->toi > .5f); REQUIRE(late->relative_velocity.GetDotProduct(late->normal) < 0);
  c = {42,Capsule{{-1,0,1},{1,0,1},.03f},Capsule{{-1,-.1f,1},{1,-.2f,1},.03f},{}};
  REQUIRE_FALSE(SweepBall(At({0,.14f,1},{0,2,0}),c,dt,.11f));
  const auto overlap = AdvanceBallTick(At({0,.135f,1},{1,2,0}),std::array{c},dt,football::model::BallConfig{},Free());
  REQUIRE_FALSE(overlap.contacts.empty()); REQUIRE(overlap.contacts.front().position_corrected);
  for (const auto& contact : overlap.contacts) REQUIRE(contact.normal_impulse == 0);
  REQUIRE(overlap.state.position.coords[0] >= .01f-1e-6f);
  REQUIRE(overlap.state.position.coords[1] > .15f);
}
TEST_CASE("thin fast rotating capsule cannot tunnel between fixed temporal samples", "[ball][deforming-ccd]") {
  const ColliderMotion c{42,Capsule{{-500,0,1},{500,0,1},.001f},Capsule{{0,-500,1},{0,500,1},.001f},{}};
  const auto ball = At({200,150,1});
  REQUIRE(Gap(ball,c,0,.001f) > 100); REQUIRE(Gap(ball,c,1,.001f) > 100);
  const auto hit = SweepBall(ball,c,dt,.001f); REQUIRE(hit.has_value());
  REQUIRE(std::fabs(hit->toi-3.f/7.f) < 1e-5f);
  REQUIRE(hit->relative_velocity.GetDotProduct(hit->normal) < -10000);
}
TEST_CASE("rotating CCD is replay deterministic mirrored and globally ordered", "[ball][deforming-ccd]") {
  const auto c = Turn(); const auto ball = At({.35f,.35f,1});
  const auto hit = SweepBall(ball,c,dt,.11f); REQUIRE(hit.has_value());
  auto mirrored_ball = ball; mirrored_ball.position.Mirror(); mirrored_ball.velocity.Mirror();
  const auto mirror = SweepBall(mirrored_ball,Mirror(c),dt,.11f); REQUIRE(mirror.has_value());
  REQUIRE(mirror->toi == hit->toi);
  auto expected = hit->surface_velocity; expected.Mirror(); REQUIRE(expected == mirror->surface_velocity);
  auto n = hit->normal; n.Mirror(); REQUIRE(n == mirror->normal);
  auto second = c; second.id = 43;
  std::array world{second,c};
  for (int repeat = 0; repeat < 100; ++repeat) {
    std::reverse(world.begin(),world.end());
    const auto winner = FirstContact(ball,world,dt,.11f); REQUIRE(winner.has_value());
    REQUIRE(winner->collider == 42); REQUIRE(winner->toi == hit->toi);
    REQUIRE(winner->surface_velocity == hit->surface_velocity);
  }
  // Earlier real static impact wins, not just another player's moving shape.
  auto low = ball; low.position.coords[2] = .13f; low.velocity = {0,0,-10};
  auto leg = c; std::get<Capsule>(leg.start).a.coords[2] = .13f; std::get<Capsule>(leg.start).b.coords[2] = .13f;
  std::get<Capsule>(leg.end).a.coords[2] = .13f; std::get<Capsule>(leg.end).b.coords[2] = .13f;
  const ColliderMotion ground{1,Plane{{0,0,0},{0,0,1}},Plane{{0,0,0},{0,0,1}},{.5f,.2f}};
  const auto result = AdvanceBallTick(low,std::array{leg,ground},dt,football::model::BallConfig{},Free());
  REQUIRE(result.contacts.size() == 1); REQUIRE(result.contacts.front().collider == 1);
  REQUIRE(result.contacts.front().normal_impulse > 0);
}
TEST_CASE("deforming CCD agrees with independent dense distance oracle", "[ball][deforming-ccd]") {
  int crossings = 0, misses = 0;
  for (int i = 1; i <= 400; ++i) {
    const float f = float(i);
    const Vector3 a{std::sin(f*.71f),std::cos(f*.37f),2+std::sin(f*.43f)};
    const Vector3 b{std::cos(f*.51f),std::sin(f*.79f),2+std::cos(f*.67f)};
    const Vector3 end_a{std::cos(f*.61f),std::sin(f*.89f),2+std::cos(f*.31f)};
    const Vector3 end_b{std::sin(f*.41f),std::cos(f*.97f),2+std::sin(f*.53f)};
    const ColliderMotion c{42,Capsule{a,b,.08f},Capsule{end_a,end_b,.12f},{}};
    const auto ball = At({std::cos(f*.19f),std::sin(f*.23f),2+std::cos(f*.29f)},
                         {20*std::sin(f*.83f),20*std::cos(f*.17f),10*std::sin(f*.13f)});
    const auto hit = SweepBall(ball,c,dt,.11f);
    float min_gap = 100, first_inside = -1;
    for (int step = 0; step <= 4000; ++step) {
      const float t = float(step)/4000, gap = Gap(ball,c,t,.11f); min_gap = std::min(min_gap,gap);
      if (gap < -1e-4f && first_inside < 0) first_inside = t;
    }
    if (first_inside >= 0) {
      ++crossings; INFO(i); REQUIRE(hit.has_value()); REQUIRE(hit->toi <= first_inside+1e-5f);
    }
    if (min_gap > .001f) { ++misses; INFO(i); REQUIRE_FALSE(hit.has_value()); }
    if (hit) {
      REQUIRE(std::isfinite(hit->surface_velocity.GetLength()));
      if (hit->toi > 0) REQUIRE(std::fabs(Gap(ball,c,hit->toi,.11f)) < 1e-5f);
    }
  }
  REQUIRE(crossings > 20); REQUIRE(misses > 20);
}

TEST_CASE("early rotating TOI and zero-length start retain correct geometry", "[ball][deforming-ccd]") {
  const ColliderMotion early{42,Capsule{{-1,0,1},{1,0,1},.03f},
      Capsule{{-1,1,1},{1,2,1},.03f},{.35f,.45f}};
  const auto ball = At({0,.14001f,1});
  const auto hit = SweepBall(ball,early,dt,.11f); REQUIRE(hit.has_value());
  REQUIRE(hit->toi > 0); REQUIRE(hit->toi < 1e-4f);
  const auto result = AdvanceBallTick(ball,std::array{early},dt,football::model::BallConfig{},Free());
  REQUIRE(result.contacts.size() == 1); REQUIRE(result.contacts[0].normal_impulse > 0);
  REQUIRE((result.state.position-hit->point).GetLength() < 1e-6f);
  const ColliderMotion grow{42,Capsule{{0,0,1},{0,0,1},.04f},
      Capsule{{-1,0,1},{1,0,1},.04f},{}};
  const auto from_point = SweepBall(At({.35f,0,1}),grow,dt,.11f); REQUIRE(from_point.has_value());
  REQUIRE(std::fabs(from_point->toi-.2f) < 1e-6f);
  REQUIRE((from_point->surface_velocity-Vector3(100,0,0)).GetLength() < 1e-4f);
}

TEST_CASE("spatial rotation and common translation preserve deforming surface evidence", "[ball][deforming-ccd]") {
  const auto c = Turn(); const auto ball = At({.35f,.35f,1});
  const auto hit = SweepBall(ball,c,dt,.11f); REQUIRE(hit.has_value());
  blunted::Quaternion rotation; rotation.SetAngleAxis(.7f,Vector3(.3f,.4f,.5f).GetNormalized());
  const Vector3 translation{3,-2,5};
  const auto point = [&](Vector3 p) { return rotation*p+translation; };
  auto transformed = c;
  for (auto* shape : {&transformed.start,&transformed.end}) {
    auto& capsule = std::get<Capsule>(*shape); capsule.a=point(capsule.a); capsule.b=point(capsule.b);
  }
  auto other = ball; other.position = point(ball.position); other.velocity=rotation*ball.velocity;
  const auto rotated = SweepBall(other,transformed,dt,.11f); REQUIRE(rotated.has_value());
  REQUIRE(std::fabs(rotated->toi-hit->toi) < 2e-6f);
  REQUIRE((rotated->normal-rotation*hit->normal).GetLength() < 2e-5f);
  REQUIRE((rotated->surface_velocity-rotation*hit->surface_velocity).GetLength() < .002f);
}

TEST_CASE("invalid deforming input is rejected before state or force commit", "[ball][deforming-ccd]") {
  const auto initial = At({.35f,.35f,1});
  auto c = Turn();
  std::get<Capsule>(c.end).radius = -.1f;
  REQUIRE_THROWS_AS(SweepBall(initial,c,dt,.11f),std::invalid_argument);
  std::get<Capsule>(c.end).radius = std::numeric_limits<float>::quiet_NaN();
  REQUIRE_THROWS_AS(SweepBall(initial,c,dt,.11f),std::invalid_argument);
  Ball a{football::model::Pitch{}}, b{football::model::Pitch{}};
  a.Reset(initial); b.Reset(initial); a.ApplyForce({2,0,0}); b.ApplyForce({2,0,0});
  REQUIRE_THROWS_AS(a.Step(BallTickInput{std::array{c},{}}),std::invalid_argument);
  REQUIRE(a.state().position == initial.position); REQUIRE(a.state().velocity == initial.velocity);
  REQUIRE(a.state().angular_velocity == initial.angular_velocity);
  REQUIRE_FALSE(a.state().orientation != initial.orientation);
  const auto after = a.Step(BallTickInput{}), control = b.Step(BallTickInput{});
  REQUIRE(after.state.position == control.state.position); REQUIRE(after.state.velocity == control.state.velocity);
}

TEST_CASE("exact deforming end-cap tangency is not an incoming impact", "[ball][deforming-ccd]") {
  const ColliderMotion c{42,Capsule{{-1,0,1},{0,1,1},.125f},
      Capsule{{1,0,1},{0,3,1},.125f},{}};
  // Both radii and the tangent position are exact binary fractions. At t=.25
  // the ball grazes cap A; changing axis orientation adds only tangential speed.
  REQUIRE_FALSE(SweepBall(At({-.5f,-.25f,1}),c,dt,.125f));
}
