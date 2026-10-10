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
      if (full) SimulationAccess::EnableBodyPhysicsShadow(simulation, true);
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
        PrintDelta("unified_minus_static_position_m", p.position_delta);
        PrintDelta("unified_minus_static_velocity_mps", p.velocity_delta);
        PrintDelta("unified_minus_static_spin_radps", p.spin_delta);
        PrintDelta("unified_minus_production_position_m", p.production_position_delta);
        PrintDelta("unified_minus_production_velocity_mps", p.production_velocity_delta);
        PrintDelta("unified_minus_production_spin_radps", p.production_spin_delta);
      }
    }
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
