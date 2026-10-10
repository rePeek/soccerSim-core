#include <array>
#include <cmath>
#include <span>

#include <catch2/catch_test_macros.hpp>

#include "sim/player/player_body_collider_motion.hpp"

namespace {
using blunted::Vector3;
using football::ball::Capsule;
using football::ball::ColliderMotion;

TEST_CASE("body collider motions retain rest-pose geometry and stable ids",
          "[sim][player][body-collider]") {
  PlayerKinematicState start;
  start.position = Vector3(4.0f, -2.0f, 0.0f);
  PlayerKinematicState end = start;
  end.position += Vector3(0.08f, -0.03f, 0.0f);

  std::array<ColliderMotion, kPlayerBodyPartCount> motions;
  const auto ids = PlayerBodyColliderIdsForSlot(17);
  BuildBodyColliderMotions(start, end, ids, motions);

  REQUIRE(ids[PlayerBodyPart::UpperBody] == kFirstDynamicBodyColliderId + 51);
  REQUIRE(ids[PlayerBodyPart::LowerBody] == kFirstDynamicBodyColliderId + 52);
  REQUIRE(ids[PlayerBodyPart::Head] == kFirstDynamicBodyColliderId + 53);
  REQUIRE(motions[0].id == ids[PlayerBodyPart::UpperBody]);
  REQUIRE(motions[1].id == ids[PlayerBodyPart::LowerBody]);
  REQUIRE(motions[2].id == ids[PlayerBodyPart::Head]);

  const auto& upper_start = std::get<Capsule>(motions[0].start);
  const auto& upper_end = std::get<Capsule>(motions[0].end);
  REQUIRE(upper_start.a == Vector3(4.0f, -2.0f, 0.81f));
  REQUIRE(upper_start.b == Vector3(4.0f, -2.0f, 1.41f));
  REQUIRE(upper_start.radius == 0.22f);
  REQUIRE(upper_end.a == upper_start.a + Vector3(0.08f, -0.03f, 0.0f));
  REQUIRE(upper_end.b == upper_start.b + Vector3(0.08f, -0.03f, 0.0f));

  const auto& lower_start = std::get<Capsule>(motions[1].start);
  REQUIRE(lower_start.a == Vector3(4.0f, -2.0f, 0.05f));
  REQUIRE(lower_start.b == Vector3(4.0f, -2.0f, 1.15f));
  REQUIRE(lower_start.radius == 0.19f);

  const auto& head_start = std::get<Capsule>(motions[2].start);
  REQUIRE(head_start.a == Vector3(4.0f, -2.0f, 1.61f));
  REQUIRE(head_start.b == head_start.a);
  REQUIRE(head_start.radius == 0.11f);
}

TEST_CASE("body collider motion requires exactly three stable output slots",
          "[sim][player][body-collider]") {
  std::array<ColliderMotion, 2> output;
  REQUIRE_THROWS_AS(BuildBodyColliderMotions(PlayerKinematicState{},
                                               PlayerKinematicState{}, output),
                    std::invalid_argument);
}

}  // namespace
