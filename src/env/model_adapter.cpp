// Keep episode validation enabled across release builds.
#undef NDEBUG

#include "env/model_adapter.hpp"

#include "data/model_adapter.hpp"
#include "env/main.hpp"
#include "sim/gamedefines.hpp"
#include "support/diagnostics/assert.hpp"

ScenarioConfig ToRuntimeScenario(const ScenarioConfig& scenario,
                                 const football::model::Team& home,
                                 const football::model::Team& away) {
  // Mutating the caller used to flip the sign bit of zero y on every reset.
  ScenarioConfig result = scenario;
  result.ball_position.coords[0] =
      result.ball_position.coords[0] * X_FIELD_SCALE;
  result.ball_position.coords[1] =
      result.ball_position.coords[1] * Y_FIELD_SCALE;

  CHECK(result.left_agents >= 0);
  CHECK(result.left_agents <= kPlayersPerTeam);
  CHECK(result.right_agents >= 0);
  CHECK(result.right_agents <= kPlayersPerTeam);

  // Episode positions override model positions, never the retained roster.
  if (result.left_team.empty()) {
    result.left_team = ToLegacyFormation(home.formation);
  }
  if (result.right_team.empty()) {
    result.right_team = ToLegacyFormation(away.formation);
  }
  return result;
}
