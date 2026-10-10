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

int main(int argc, char** argv) {
  try {
    const int steps = argc > 1 ? std::stoi(argv[1]) : 4000;
    if (steps <= 0) throw std::invalid_argument("steps must be positive");
    const auto home = football::app::fixtures::MakeDefaultHomeTeam();
    const auto away = football::app::fixtures::MakeDefaultAwayTeam();
    for (bool reverse : {false, true}) {
      MatchOptions options;
      options.reverse_team_processing = reverse;
      options.snapshot_capacity = 4;
      options.game_engine_random_seed = 42;
      Simulation simulation;
      simulation.Init(home, away, football::model::Pitch{}, options);
      football::ai::DefaultAI policy(home, away, football::model::Pitch{});
      PlayerControlSet controls;
      for (int i = 0; i < steps && !simulation.Finished(); ++i) {
        policy.Update(simulation.Observe(), controls);
        simulation.Step(controls);
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
    }
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
