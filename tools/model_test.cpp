#include <cstdlib>
#include <type_traits>

#include "model/formation.hpp"
#include "model/ids.hpp"
#include "model/pitch.hpp"
#include "model/player.hpp"
#include "model/team.hpp"

int main() {
  namespace model = football::model;
  static_assert(!std::is_same_v<model::PlayerId, model::PlayerDatabaseId>);
  constexpr model::Pitch pitch = model::MakeLegacyPitch();
  static_assert(pitch.length() == 110.0f && pitch.width() == 72.0f);
  static_assert(pitch.contains(55.0f, 36.0f));
  static_assert(pitch.contains(-55.0f, -36.0f));
  static_assert(!pitch.contains(55.01f, 0.0f));
  static_assert(!pitch.contains(0.0f, -36.01f));

  model::Team home = model::MakeDefaultHomeTeam();
  model::Team away = model::MakeDefaultAwayTeam();
  if (home.name != "Frequentists United" || away.name != "Real Bayesians" ||
      home.players.size() != kPlayersPerTeam ||
      away.players.size() != kPlayersPerTeam ||
      home.players.front().database_id != 398) {
    return EXIT_FAILURE;
  }

  home.formation.push_back({{-0.5f, 0.25f}, e_PlayerRole_CM, false, true});
  model::Team copy = home;
  copy.players.front().database_id = 11;
  copy.formation.front().position.y = -0.25f;
  if (home.players.front().database_id != 398 ||
      home.formation.front().position.y != 0.25f) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
