#include <catch2/catch_test_macros.hpp>

#include "sim/observation/player_slots.hpp"
#include "sim/referee/ball_rules.hpp"

namespace {

using football::model::Pitch;
using football::sim::BallOutOfPlay;
using football::sim::ClassifyBallOutOfPlay;
using football::sim::CrossedGoalMouth;

TEST_CASE("ball out-of-play classification preserves line priority",
          "[sim][referee][query]") {
  const Pitch pitch{};
  constexpr float kMargin = 0.11f;
  const float x_limit = pitch.half_length() + pitch.line_half_width() + kMargin;
  const float y_limit = pitch.half_width() + pitch.line_half_width() + kMargin;

  REQUIRE(ClassifyBallOutOfPlay(pitch, {0, 0, 0}) == BallOutOfPlay::None);
  REQUIRE(ClassifyBallOutOfPlay(pitch, {x_limit - 0.01f, 0, 0}) == BallOutOfPlay::None);
  REQUIRE(ClassifyBallOutOfPlay(pitch, {x_limit + 0.01f, 0, 0}) == BallOutOfPlay::GoalLine);
  REQUIRE(ClassifyBallOutOfPlay(pitch, {-x_limit - 0.01f, 0, 0}) == BallOutOfPlay::GoalLine);
  REQUIRE(ClassifyBallOutOfPlay(pitch, {0, y_limit + 0.01f, 0}) == BallOutOfPlay::Touchline);
  REQUIRE(ClassifyBallOutOfPlay(pitch, {0, -y_limit - 0.01f, 0}) == BallOutOfPlay::Touchline);
  // The goal line wins when both conditions could apply.
  REQUIRE(ClassifyBallOutOfPlay(pitch, {x_limit + 1.0f, y_limit + 1.0f, 0}) ==
          BallOutOfPlay::GoalLine);
}

TEST_CASE("goal-mouth crossing reports the crossed side between snapshots",
          "[sim][referee][query]") {
  const Pitch pitch{};
  const float x = pitch.half_length() + pitch.line_half_width() + 0.11f;

  auto positive = CrossedGoalMouth(pitch, {x - 1.0f, 0, 0.1f}, {x + 1.0f, 0, 0.1f});
  REQUIRE(positive.has_value());
  REQUIRE(*positive == 1);

  auto negative = CrossedGoalMouth(pitch, {-x + 1.0f, 0, 0.1f}, {-x - 1.0f, 0, 0.1f});
  REQUIRE(negative.has_value());
  REQUIRE(*negative == -1);

  // Running along the goal line without crossing it is not a goal.
  REQUIRE_FALSE(
      CrossedGoalMouth(pitch, {x - 1.0f, -1.0f, 0.1f}, {x - 1.0f, 1.0f, 0.1f}).has_value());
  // A ball outside the posts does not score (legacy side-net veto).
  REQUIRE_FALSE(CrossedGoalMouth(pitch, {x - 1.0f, pitch.goal_half_width() + 2.0f, 0.1f},
                                 {x + 1.0f, pitch.goal_half_width() + 2.0f, 0.1f})
                    .has_value());
}

TEST_CASE("player slot table maps identities to stable team sides",
          "[sim][observation][query]") {
  football::sim::observation::PlayerSlotTable slots;
  slots.player_ids = {10u, 11u, 20u};
  slots.team_sides = {football::model::TeamSide::Home,
                      football::model::TeamSide::Home,
                      football::model::TeamSide::Away};

  REQUIRE(slots.Size() == 3u);
  REQUIRE(slots.SlotOf(11u) == 1u);
  REQUIRE(slots.SideOf(10u) == football::model::TeamSide::Home);
  REQUIRE(slots.SideOf(20u) == football::model::TeamSide::Away);
  REQUIRE_FALSE(slots.SlotOf(99u).has_value());
  REQUIRE_FALSE(slots.SideOf(99u).has_value());
}

}  // namespace
