// ANIM-2 A14.0 — Trip clip + entry-frame matching.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "football/animation/trip_match.hpp"

using namespace football::animation;
using football::math::Vector3;

namespace {

std::vector<int> TripClips(const AnimationLibrary& library) {
  std::vector<int> trips;
  for (const AnimationClip& clip : library.Clips()) {
    if (clip.metadata.action_type == e_DefString_Trip) {
      trips.push_back(static_cast<int>(clip.id));
    }
  }
  return trips;
}

float FallAlignment(const AnimationLibrary& library, int clip_id,
                    const Vector3& rel_impact) {
  const AnimationClip& clip = library.Get(static_cast<uint32_t>(clip_id));
  const Vector3 fall =
      clip.metadata.bump_direction.GetNormalized(clip.metadata.incoming_body_direction);
  if (fall.GetLength() <= 0.001f) return 0.0f;
  return fall.GetDotProduct(rel_impact);
}

}  // namespace

TEST_CASE("SelectTripMatch is deterministic and returns a legal entry frame") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_BAKED_ANIM_PATH));
  const std::vector<int> trips = TripClips(library);
  REQUIRE(!trips.empty());

  TripMatchRequest request;
  request.impact_direction = Vector3(0, -1, 0);
  request.current_velocity = Vector3(0, -5, 0);
  request.spatial_angle = 0.0f;
  request.trip_type = -1;

  const TripMatchResult first = SelectTripMatch(library, trips, request);
  REQUIRE(first.matched);
  REQUIRE(first.clip_id >= 0);
  const TripMatchResult second = SelectTripMatch(library, trips, request);
  CHECK(second.clip_id == first.clip_id);
  CHECK(second.start_frame == first.start_frame);
}

TEST_CASE("SelectTripMatch honours the trip type filter") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_BAKED_ANIM_PATH));
  const std::vector<int> trips = TripClips(library);
  REQUIRE(!trips.empty());

  int requested_type = library.Get(static_cast<uint32_t>(trips.front())).metadata.trip_type;
  bool have_type = false;
  for (const int id : trips) {
    if (library.Get(static_cast<uint32_t>(id)).metadata.trip_type != requested_type) {
      have_type = true;
      break;
    }
  }
  if (!have_type) return;  // the asset only carries one trip type; nothing to filter

  std::vector<int> typed;
  for (const int id : trips) {
    if (library.Get(static_cast<uint32_t>(id)).metadata.trip_type == requested_type)
      typed.push_back(id);
  }
  REQUIRE(!typed.empty());

  TripMatchRequest request;
  request.impact_direction = Vector3(0, -1, 0);
  request.current_velocity = Vector3(0, -3, 0);
  request.trip_type = requested_type;
  const TripMatchResult result = SelectTripMatch(library, trips, request);
  REQUIRE(result.matched);
  CHECK(library.Get(static_cast<uint32_t>(result.clip_id)).metadata.trip_type ==
        requested_type);
}

TEST_CASE("SelectTripMatch prefers a fall aligned with the impact") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_BAKED_ANIM_PATH));
  const std::vector<int> trips = TripClips(library);
  REQUIRE(!trips.empty());

  const Vector3 impact(0, -1, 0);
  TripMatchRequest request;
  request.impact_direction = impact;
  request.current_velocity = Vector3(0, -4, 0);
  request.spatial_angle = 0.0f;
  const TripMatchResult result = SelectTripMatch(library, trips, request);
  REQUIRE(result.matched);

  // Direction has the largest weight, so the winner must not fall against the
  // impact when an aligned candidate exists.
  float best_alignment = -2.0f;
  for (const int id : trips) {
    best_alignment = std::max(best_alignment, FallAlignment(library, id, impact));
  }
  if (best_alignment > 0.2f) {
    CHECK(FallAlignment(library, result.clip_id, impact) > 0.0f);
  }
}

TEST_CASE("SelectTripMatch uses the current pose for the entry frame") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_BAKED_ANIM_PATH));
  const std::vector<int> trips = TripClips(library);
  REQUIRE(!trips.empty());

  const AnimationClip& clip = library.Get(static_cast<uint32_t>(trips.front()));
  REQUIRE(clip.poses.size() > 5);

  std::vector<int> single{trips.front()};
  TripMatchRequest request;
  request.impact_direction = Vector3(0, -1, 0);
  request.current_velocity = Vector3(0, -2, 0);
  request.current_pose = clip.poses[3];
  request.has_current_pose = true;
  request.trip_type = clip.metadata.trip_type;
  const TripMatchResult result = SelectTripMatch(library, single, request);
  REQUIRE(result.matched);
  CHECK(result.clip_id == trips.front());
  CHECK(result.start_frame >= 0);
  CHECK(result.start_frame <= static_cast<int>(clip.poses.size()) - 2);
}

TEST_CASE("SelectTripMatch reserves the fall process") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_BAKED_ANIM_PATH));
  const std::vector<int> trips = TripClips(library);
  REQUIRE(!trips.empty());

  TripMatchRequest request;
  request.impact_direction = Vector3(0, -1, 0);
  request.current_velocity = Vector3(0, -4, 0);
  const TripMatchResult result = SelectTripMatch(library, trips, request);
  REQUIRE(result.matched);
  const AnimationClip& clip = library.Get(static_cast<uint32_t>(result.clip_id));
  const int frame_count = static_cast<int>(clip.poses.size());
  // The entry stays in the first half, so the fall itself still plays.
  CHECK(result.start_frame <= frame_count - 1 - frame_count / 2);
}
