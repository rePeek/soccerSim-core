#include <algorithm>
#include <bit>
#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "ai/default_ai.hpp"
#include "app/fixtures/default_teams.hpp"
#include "sim/testing/simulation_access.hpp"

using football::sim::testing::SimulationAccess;
namespace {
struct Run {
  std::uint64_t hash = 14695981039346656037ULL;
  std::uint64_t steps = 0, body_touches = 0, active_touches = 0, constraints = 0;
  MatchResult result;
  std::string rng;
  double mean_us = 0, p99_us = 0, max_us = 0;
};
Run Play(football::sim::TickSpan half, bool reverse) {
  auto home = football::app::fixtures::MakeDefaultHomeTeam();
  auto away = football::app::fixtures::MakeDefaultAwayTeam();
  MatchOptions options; options.half_duration = half; options.snapshot_capacity = 2;
  options.reverse_team_processing = reverse;
  Simulation s; s.Init(home, away, football::model::Pitch{}, options);
  SimulationAccess::EnableActiveImpulseProduction(s, true);
  SimulationAccess::EnableBodyPhysicsProduction(s, true);
  football::ai::DefaultAI policy(home, away, football::model::Pitch{});
  Run out;
  std::vector<double> durations;
  const auto hash = [&](std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
      out.hash ^= (value >> (i * 8)) & 255; out.hash *= 1099511628211ULL;
    }
  };
  const auto vector = [&](const blunted::Vector3& v) {
    for (float x : v.coords) hash(std::bit_cast<std::uint32_t>(x));
  };
  while (!s.Finished()) {
    if (out.steps > 10000000) throw std::runtime_error("match did not reach its referee terminal state");
    PlayerControlSet controls; policy.Update(s.Observe(), controls);
    const auto start = std::chrono::steady_clock::now();
    s.Step(controls);
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
    durations.push_back(us); out.mean_us += us; ++out.steps;
    const auto& record = *s.Snapshots().Latest();
    hash(record.stamp.step_index); hash(record.stamp.timeline_tick.value); hash(record.stamp.generation);
    vector(record.snapshot.ball.position); vector(record.snapshot.ball.velocity);
    vector(record.snapshot.ball.angular_velocity);
    for (const auto& p : record.snapshot.players) {
      vector(p.position); vector(p.velocity); vector(p.facing); vector(p.body_facing);
      hash(p.animation_id); hash(p.frame); hash(p.active); hash(p.has_possession);
    }
    const auto& commit = SimulationAccess::CommittedBallTickOf(s);
    if (commit && commit->endpoint_constraint) ++out.constraints;
    for (const auto& touch : SimulationAccess::RuleTouchesOf(s)) {
      hash(touch.accepted.player); hash(touch.accepted.touched_at.value);
      hash(static_cast<unsigned>(touch.accepted.type)); hash(static_cast<unsigned>(touch.source));
      hash(static_cast<unsigned>(touch.part)); hash(touch.step); hash(touch.generation);
      if (touch.source == football::sim::event::RuleTouchSource::BodyCCD) ++out.body_touches;
      if (touch.source == football::sim::event::RuleTouchSource::PreparedAction) ++out.active_touches;
    }
  }
  out.result = s.Result();
  std::ostringstream rng; rng << SimulationAccess::RngOf(s).engine(); out.rng = rng.str();
  out.mean_us /= out.steps;
  std::sort(durations.begin(), durations.end());
  out.p99_us = durations[(durations.size() - 1) * 99 / 100];
  out.max_us = durations.back();
  return out;
}
}
int main(int argc, char** argv) {
  try {
    const auto half = argc > 1 && std::string(argv[1]) == "full" ? football::sim::Minutes(45) :
        football::sim::Seconds(argc > 1 ? std::stoul(argv[1]) : 20);
    const bool reverse = argc > 2 && std::string(argv[2]) == "reverse";
    const auto a = Play(half, reverse);
    const auto b = Play(half, reverse);
    if (a.hash != b.hash || a.rng != b.rng || a.steps != b.steps ||
        a.body_touches != b.body_touches || a.active_touches != b.active_touches)
      throw std::runtime_error("unified complete-match replay differs");
    std::cout << "half_ticks=" << half.value << " reverse=" << reverse
              << " steps=" << a.steps << " hash=" << a.hash
              << " ccd_rule_touches=" << a.body_touches << " active_rule_touches=" << a.active_touches
              << " constraint_ticks=" << a.constraints << " mean_step_us=" << a.mean_us
              << " p99_step_us=" << a.p99_us << " max_step_us=" << a.max_us
              << " replay=exact\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n'; return 1;
  }
}
