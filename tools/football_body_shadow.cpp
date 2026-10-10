// Read-only P4 body collision diagnostics. No contact is fed back to gameplay.
#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <string>
#include "ai/default_ai.hpp"
#include "app/fixtures/default_teams.hpp"
#include "sim/testing/simulation_access.hpp"

using football::sim::testing::SimulationAccess;

void PrintErrors(const char* category, const BodyEndpointErrors& errors) {
  std::cout << category << ",samples=" << errors.samples << ",mean_m="
            << (errors.samples ? errors.sum / errors.samples : 0)
            << ",max_m=" << errors.maximum << ",bins=";
  for (auto count : errors.bins) std::cout << count << ':';
  std::cout << '\n';
}

void PrintDelta(const char* quantity, const BodyEndpointErrors& delta) {
  std::cout << quantity << ",mean=" << (delta.samples ? delta.sum / delta.samples : 0)
            << ",max=" << delta.maximum << '\n';
}

int main(int argc, char** argv) {
  try {
    const int steps = argc > 1 ? std::stoi(argv[1]) : 4000;
    if (steps <= 0) throw std::invalid_argument("steps must be positive");
    const bool full = argc > 2 && std::string(argv[2]) == "--full";
    if (argc > 3 || (argc > 2 && !full)) throw std::invalid_argument("usage: football_body_shadow [steps] [--full]");
    const auto home = football::app::fixtures::MakeDefaultHomeTeam();
    const auto away = football::app::fixtures::MakeDefaultAwayTeam();
    for (bool reverse : {false, true}) {
      MatchOptions options;
      options.reverse_team_processing = reverse;
      options.snapshot_capacity = 4;
      options.game_engine_random_seed = 42;
      Simulation simulation;
      simulation.Init(home, away, football::model::Pitch{}, options);
      if (full) {
        SimulationAccess::EnableBodyPhysicsShadow(simulation, true);
        SimulationAccess::EnableActiveTouchShadow(simulation, true);
      }
      int contact_samples = 0;
      football::ai::DefaultAI policy(home, away, football::model::Pitch{});
      PlayerControlSet controls;
      for (int i = 0; i < steps && !simulation.Finished(); ++i) {
        policy.Update(simulation.Observe(), controls);
        simulation.Step(controls);
        const auto& evidence = SimulationAccess::BodyPhysicsShadowLatestOf(simulation);
        if (full && evidence && evidence->step_index == simulation.Snapshots().Latest()->stamp.step_index &&
            contact_samples < 8 && !evidence->unified.contacts.empty()) {
          const auto& hit = evidence->unified.contacts.front();
          if (hit.collider != 1) {
            ++contact_samples;
            std::cout << "contact,reverse=" << reverse << ",step=" << evidence->step_index
                      << ",collider=" << hit.collider << ",toi=" << hit.toi
                      << ",center=" << hit.point << ",jn=" << hit.normal_impulse
                      << ",player=" << (evidence->player ? std::to_string(*evidence->player) : "pitch")
                      << ",part=" << (evidence->part ? std::to_string(static_cast<int>(*evidence->part)) : "NA")
                      << ",dv=" << evidence->unified.state.velocity - evidence->static_only.state.velocity
                      << ",dw=" << evidence->unified.state.angular_velocity - evidence->static_only.state.angular_velocity << '\n';
            if (evidence->classification) {
              const auto& c = *evidence->classification;
              std::cout << "classification,action=" << kBodyActionNames[c.action]
                        << ",window=" << static_cast<int>(c.window) << ",blocks=" << c.legacy_blocks
                        << ",penetration=" << c.initial_penetration << ",continued=" << c.continued_geometry
                        << ",episode_length=" << c.geometry_length << '\n';
            }
          }
        }
      }
      const auto& r = SimulationAccess::BodyCollisionShadowReportOf(simulation);
      std::cout << std::setprecision(8) << "body-shadow,seed=42,reverse=" << reverse
                << ",steps=" << steps << ",ticks=" << r.predicted_ticks
                << ",discarded=" << r.discarded_ticks << ",players=" << r.predicted_players
                << ",first=" << r.first_contacts << ",accepted=" << r.accepted_accidental_touches
                << ",matched=" << r.matched_touches << ",missed=" << r.missed_touches
                << ",spurious=" << r.false_positive_contacts
                << ",pending_lower_contacts=" << r.action_phase_conflicts << '\n';
      PrintErrors("all", r.endpoint_errors);
      const char* categories[] = {"movement", "sliding", "trip", "other_animation"};
      for (int i = 0; i < 4; ++i) PrintErrors(categories[i], r.action_errors[i]);
      PrintErrors("turning", r.turning_errors);
      if (full) {
        const auto& p = SimulationAccess::BodyPhysicsShadowReportOf(simulation);
        std::cout << "physics-shadow,reverse=" << reverse << ",ticks=" << p.ticks
                  << ",static_first=" << p.static_first << ",body_first=" << p.dynamic_first
                  << ",effective_body=" << p.effective_body_impacts << ",zero_impulse=" << p.zero_impulse_contacts
                  << ",superseded=" << p.dynamic_query_superseded << ",matched=" << p.matched_touches
                  << ",missed=" << p.missed_touches << ",unmatched_impacts=" << p.unmatched_impacts << '\n';
        std::cout << "episodes,reverse=" << reverse << ",geometry=" << p.geometric_contacts
                  << ",starts=" << p.geometric_episodes << ",continued=" << p.geometric_continued
                  << ",longest=" << p.longest_geometric_episode << ",impact_starts=" << p.impact_episodes
                  << ",impact_continued=" << p.impact_continued << '\n';
        for (std::size_t action = 0; action < kBodyActionCount; ++action) {
          const auto& g = p.action_groups[action];
          if (!p.player_action_samples[action]) continue;
          std::cout << "impact-group,reverse=" << reverse << ",action=" << kBodyActionNames[action]
                    << ",player_ticks=" << p.player_action_samples[action]
                    << ",impacts=" << g.impacts << ",unmatched=" << g.unmatched
                    << ",first=" << g.first << ",continued=" << g.continued
                    << ",penetration=" << g.penetration << ",corrected=" << g.corrected
                    << ",starts_separated=" << g.separated << ",window=" << g.boundary
                    << ",pending=" << g.pending << ",foot_conflict=" << g.foot_conflicts
                    << ",accepted_active_same_player=" << g.intervals_with_active_touch
                    << ",multi_parts=" << g.multi_parts << ",multi_players=" << g.multi_players
                    << ",mean_ball_mps=" << (g.impacts ? g.ball_speed_sum / g.impacts : 0)
                    << ",mean_player_mps=" << (g.impacts ? g.player_speed_sum / g.impacts : 0)
                    << ",mean_closing_mps=" << (g.impacts ? g.closing_speed_sum / g.impacts : 0);
          for (std::size_t bit = 0; bit < kBodyLegacyBlockCount; ++bit)
            std::cout << ',' << kBodyLegacyBlockNames[bit] << '=' << g.blocks[bit];
          std::cout << '\n';
        }
        std::cout << "pose-proposal,reverse=" << reverse << ",intervals=" << p.posed_intervals
                  << ",total_body_impacts=" << p.posed_body_impacts << ",changed_first=" << p.pose_changed_first
                  << ",removed=" << p.upright_impacts_removed << ",added=" << p.posed_impacts_added << '\n';
        PrintDelta("unified_minus_static_position_m", p.position_delta);
        PrintDelta("unified_minus_static_velocity_mps", p.velocity_delta);
        PrintDelta("unified_minus_static_spin_radps", p.spin_delta);
        PrintDelta("unified_minus_production_position_m", p.production_position_delta);
        PrintDelta("unified_minus_production_velocity_mps", p.production_velocity_delta);
        PrintDelta("unified_minus_production_spin_radps", p.production_spin_delta);
        const auto& active = SimulationAccess::ActiveTouchShadowReportOf(simulation);
        std::cout << "active-shadow,reverse=" << reverse << ",contending_ticks=" << active.contending_ticks
                  << ",duplicates=" << active.duplicate_candidates << ",dropped=" << active.dropped_details;
        for (std::size_t origin = 0; origin < active.origins.size(); ++origin)
          std::cout << ",origin_" << origin << '=' << active.origins[origin];
        std::cout << '\n';
        for (std::size_t action = 0; action < active.actions.size(); ++action) {
          const auto& a = active.actions[action];
          if (!a.pending_samples && !a.frames && !a.executed) continue;
          std::cout << "active-action,reverse=" << reverse << ",action=" << kBodyActionNames[action]
                    << ",pending=" << a.pending_samples << ",frames=" << a.frames << ",executed=" << a.executed
                    << ",no_impulse=" << a.no_impulse << ",fallback=" << a.fallback_points
                    << ",passive_endpoints=" << a.passive_endpoints << ",same_part=" << a.same_part_conflicts
                    << ",other_player=" << a.other_player_conflicts << ",position_mutations=" << a.position_mutations
                    << ",dv_max=" << a.velocity_error_max << ",dw_max=" << a.spin_error_max
                    << ",surface_error_max=" << a.surface_error_max << ",spin_max=" << a.spin_max;
          for (std::size_t reason = 0; reason < a.rejected.size(); ++reason)
            std::cout << ",reject_" << reason << '=' << a.rejected[reason];
          std::cout << '\n';
        }
      }
    }
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
