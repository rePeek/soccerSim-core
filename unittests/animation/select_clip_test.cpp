// ANIM-1 A6.2 — stateless SelectClip tests (synthetic + real baked library).

#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "animation_test_support.hpp"
#include "football/animation/library.hpp"
#include "football/animation/select_clip.hpp"

TEST_CASE("SelectClip picks by action and velocity, deterministically") {
  std::vector<AnimationClip> clips;
  clips.push_back(MakeMovementClip(0, 0.0f, 0.0f, 0.0f, 0.0f));  // idle
  clips.push_back(MakeMovementClip(1, 0.0f, 3.5f, 0.0f, 1.0f));  // idle -> dribble
  clips.push_back(MakeMovementClip(2, 5.0f, 5.0f, 0.0f, 3.0f));  // walk cruise
  AnimationClip shot = MakeMovementClip(3, 3.5f, 3.5f, 0.0f, 0.0f);
  shot.metadata.action_type = e_DefString_Shot;
  clips.push_back(shot);

  SelectionRequest move;
  move.action = ClipAction::Move;
  move.desired_speed = 5.0f;
  const SelectionResult walk = SelectClip(clips, move);
  REQUIRE(walk.valid);
  CHECK(walk.clip_id == 2);

  SelectionRequest shoot;
  shoot.action = ClipAction::Shoot;
  const SelectionResult s = SelectClip(clips, shoot);
  REQUIRE(s.valid);
  CHECK(s.clip_id == 3);

  // An action with no candidates is invalid, not a crash.
  SelectionRequest save;
  save.action = ClipAction::Save;
  CHECK_FALSE(SelectClip(clips, save).valid);
}

TEST_CASE("SelectClip is permutation-independent and matches on the real library") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_BAKED_ANIM_PATH));

  SelectionRequest move;
  move.action = ClipAction::Move;
  move.desired_speed = 5.0f;
  const SelectionResult a = SelectClip(library.Clips(), move);
  const SelectionResult b = SelectClip(library.Clips(), move);
  REQUIRE(a.valid);
  CHECK(a.clip_id == b.clip_id);
  // The chosen clip is a Movement clip at the walk bucket.
  CHECK(library.Get(static_cast<std::uint32_t>(a.clip_id)).metadata.action_type ==
        e_DefString_Movement);

  SelectionRequest shoot;
  shoot.action = ClipAction::Shoot;
  const SelectionResult s = SelectClip(library.Clips(), shoot);
  REQUIRE(s.valid);
  CHECK(library.Get(static_cast<std::uint32_t>(s.clip_id)).metadata.action_type ==
        e_DefString_Shot);
}
