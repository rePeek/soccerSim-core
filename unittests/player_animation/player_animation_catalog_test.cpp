// LOCO-2.0 — Movement animation catalog audit.
//
// Loads the baked AnimationLibrary, classifies every Movement clip by velocity
// bucket (idle/walk/sprint), acceleration/deceleration sign, turn angle and
// foot markers, and extracts per-clip reference motion features from the baked
// metadata and root_positions. Read-only: no Simulation, no Humanoid, no RNG,
// no behaviour change. The asset dependency is deliberately isolated here (not
// in the pure-math player target).

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "football/animation/clip.hpp"
#include "football/animation/library.hpp"
#include "football/animation/types.hpp"

namespace {

struct CatalogEntry {
  uint32_t id;
  std::string name;
  uint32_t frame_count;
  float incoming_velocity;
  float outgoing_velocity;
  float speed_delta;
  float duration_s;
  float total_displacement;
  float mean_speed;
  float turn_body_angle_deg;  // |outgoing_body_angle|
  float turn_dir_angle_deg;   // |incoming_body_direction vs outgoing_direction|
  bool base_animation;
  int touch_frame;
  int current_foot;
  std::string forced_foot;
  std::string touch_foot;
  bool has_incoming_special;
  bool has_outgoing_special;
  bool has_retain;
};

float Deg(float rad) { return static_cast<float>(rad) * 180.0f / football::math::pi; }

std::vector<CatalogEntry> BuildCatalog(const AnimationLibrary& library) {
  std::vector<CatalogEntry> entries;
  for (const AnimationClip& clip : library.Clips()) {
    if (clip.metadata.action_type != e_DefString_Movement) continue;

    CatalogEntry e;
    e.id = clip.id;
    e.name = clip.name;
    e.frame_count = clip.frame_count;
    const auto& m = clip.metadata;
    e.incoming_velocity = m.incoming_velocity;
    e.outgoing_velocity = m.outgoing_velocity;
    e.speed_delta = m.outgoing_velocity - m.incoming_velocity;
    e.duration_s = static_cast<float>(clip.frame_count) / 100.0f;
    e.total_displacement = 0.0f;
    if (clip.root_positions.size() >= 2) {
      const auto d = clip.root_positions.back() - clip.root_positions.front();
      e.total_displacement = std::sqrt(d.coords[0] * d.coords[0] + d.coords[1] * d.coords[1]);
    }
    e.mean_speed = e.duration_s > 0.0f ? e.total_displacement / e.duration_s : 0.0f;
    e.turn_body_angle_deg = Deg(m.outgoing_body_angle);
    const auto outgoing_body_dir = m.outgoing_direction.GetRotated2D(m.outgoing_body_angle);
    e.turn_dir_angle_deg =
        Deg(std::fabs(static_cast<float>(m.incoming_body_direction.GetAngle2D(outgoing_body_dir))));
    e.base_animation = m.base_animation;
    e.touch_frame = m.touch_frame;
    e.current_foot = m.current_foot;
    e.forced_foot = m.forced_foot;
    e.touch_foot = m.touch_foot;
    e.has_incoming_special = !m.incoming_special_state.empty();
    e.has_outgoing_special = !m.outgoing_special_state.empty();
    e.has_retain = !m.incoming_retain_state.empty() || !m.outgoing_retain_state.empty();
    entries.push_back(std::move(e));
  }
  return entries;
}

void DumpCatalog(const std::vector<CatalogEntry>& entries) {
  if (std::getenv("FOOTBALL_ANIM_CATALOG_DIAG") == nullptr) return;
  std::fprintf(stderr,
               "id,name,frame_count,in,out,speed_delta,dur_s,disp,mean_speed,"
               "turn_body_deg,turn_dir_deg,base,touch_frame,current_foot,forced_foot,"
               "touch_foot,in_special,out_special,retain\n");
  for (const auto& e : entries) {
    std::fprintf(stderr,
                 "%u,%s,%u,%.2f,%.2f,%+.2f,%.3f,%.3f,%.2f,%.1f,%.1f,%d,%d,%d,%s,%s,%d,%d,%d\n",
                 e.id, e.name.c_str(), e.frame_count, e.incoming_velocity,
                 e.outgoing_velocity, e.speed_delta, e.duration_s,
                 e.total_displacement, e.mean_speed, e.turn_body_angle_deg,
                 e.turn_dir_angle_deg, e.base_animation ? 1 : 0, e.touch_frame,
                 e.current_foot, e.forced_foot.c_str(), e.touch_foot.c_str(),
                 e.has_incoming_special ? 1 : 0, e.has_outgoing_special ? 1 : 0,
                 e.has_retain ? 1 : 0);
  }
}

int CountByIncoming(const std::vector<CatalogEntry>& entries, e_Velocity bucket) {
  int n = 0;
  for (const auto& e : entries)
    if (FloatToEnumVelocity(e.incoming_velocity) == bucket) ++n;
  return n;
}

}  // namespace

TEST_CASE("movement catalog is non-empty and covers the basic buckets") {
  AnimationLibrary library;
  REQUIRE(library.Load(GFOOTBALL_BAKED_ANIM_PATH));
  const auto entries = BuildCatalog(library);
  DumpCatalog(entries);
  REQUIRE_FALSE(entries.empty());

  const int idle = CountByIncoming(entries, e_Velocity_Idle);
  const int walk = CountByIncoming(entries, e_Velocity_Walk);
  const int sprint = CountByIncoming(entries, e_Velocity_Sprint);
  INFO("idle=" << idle << " walk=" << walk << " sprint=" << sprint);
  CHECK(idle > 0);
  CHECK(walk > 0);
  CHECK(sprint > 0);

  bool has_accel = false, has_decel = false;
  for (const auto& e : entries) {
    if (e.speed_delta > 0.3f) has_accel = true;
    if (e.speed_delta < -0.3f) has_decel = true;
  }
  CHECK(has_accel);
  CHECK(has_decel);
}
