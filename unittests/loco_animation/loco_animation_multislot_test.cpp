// ANIM-1 A4 — 22 independent playback states over one shared AnimationLibrary.
//
// Pure: no Simulation, Humanoid, Ball, Referee or RNG. Uses the real baked
// library so the shared-resource path is exercised.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "football/animation/library.hpp"
#include "sim/player/player_playback_state.hpp"

namespace {
std::vector<AnimationId> PickClips(const AnimationLibrary& library, std::size_t count) {
  std::vector<AnimationId> ids;
  for (const AnimationClip& clip : library.Clips()) {
    if (clip.metadata.action_type == e_DefString_Movement && !clip.poses.empty()) {
      ids.push_back(static_cast<AnimationId>(clip.id));
      if (ids.size() == count) break;
    }
  }
  return ids;
}
}  // namespace

TEST_CASE("22 playback states share one library and never cross-contaminate") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_BAKED_ANIM_PATH));
  const std::vector<AnimationId> ids = PickClips(library, 22);
  REQUIRE(ids.size() == 22);

  std::vector<PlayerPlaybackState> states(22);
  // Each slot plays its own clip for 12 ticks.
  for (int tick = 0; tick < 12; ++tick) {
    for (std::size_t slot = 0; slot < states.size(); ++slot) {
      states[slot].Tick(library.Get(static_cast<std::uint32_t>(ids[slot])),
                        kDefaultMovementBlendTicks);
    }
  }

  // Every slot kept its own clip; frames advanced independently.
  for (std::size_t slot = 0; slot < states.size(); ++slot) {
    CHECK(states[slot].clip_id == ids[slot]);
    CHECK(states[slot].frame > 0);
  }

  // Interrupting one slot does not touch the others.
  const AnimationId other = ids[0] == ids[1] ? ids[2] : ids[1];
  const PlayerPlaybackState snapshot = states[5];
  states[0].Tick(library.Get(static_cast<std::uint32_t>(other)), kActionBlendTicks);
  CHECK(states[5].clip_id == snapshot.clip_id);
  CHECK(states[5].frame == snapshot.frame);

  // Reset clears each slot independently.
  states[3].Reset();
  CHECK_FALSE(states[3].have_pose);
  CHECK(states[4].have_pose);
}

TEST_CASE("22 playback states replay deterministically") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_BAKED_ANIM_PATH));
  const std::vector<AnimationId> ids = PickClips(library, 22);
  REQUIRE(ids.size() == 22);

  const auto run = [&]() {
    std::vector<PlayerPlaybackState> states(22);
    for (int tick = 0; tick < 10; ++tick) {
      for (std::size_t slot = 0; slot < states.size(); ++slot) {
        states[slot].Tick(library.Get(static_cast<std::uint32_t>(ids[slot])),
                          kDefaultMovementBlendTicks);
      }
    }
    std::vector<PoseFrame> poses;
    for (const auto& s : states) poses.push_back(s.final_pose);
    return poses;
  };

  const std::vector<PoseFrame> a = run();
  const std::vector<PoseFrame> b = run();
  REQUIRE(a.size() == b.size());
  for (std::size_t slot = 0; slot < a.size(); ++slot) {
    for (std::size_t j = 0; j < kBodyPartCount; ++j) {
      for (int e = 0; e < 4; ++e) {
        REQUIRE(a[slot].orientations[j].elements[e] == b[slot].orientations[j].elements[e]);
        REQUIRE(std::isfinite(a[slot].orientations[j].elements[e]));
      }
    }
  }
}
