#include <cstdlib>
#include <stdexcept>
#include <type_traits>

#include "model/formation.hpp"
#include "model/pitch.hpp"
#include "model/player.hpp"
#include "model/team.hpp"

int main() {
  namespace model = football::model;
  static_assert(!std::is_same_v<model::PlayerId, model::PlayerDatabaseId>);
  static_assert(model::TeamSide::Home != model::TeamSide::Away);
  static_assert(std::is_same_v<std::underlying_type_t<model::TeamSide>, std::uint8_t>);
  static_assert(model::kPlayerStatCount == 22);
  constexpr model::Pitch pitch = model::MakeLegacyPitch();
  static_assert(pitch.length() == 110.0f && pitch.width() == 72.0f);
  static_assert(pitch.contains(55.0f, 36.0f));
  static_assert(pitch.contains(-55.0f, -36.0f));
  static_assert(!pitch.contains(55.01f, 0.0f));
  static_assert(!pitch.contains(0.0f, -36.01f));

  constexpr auto attributes = [] {
    model::PlayerAttributes value;
    value.set(model::PlayerStat::technical_shortpass, 0.8765432f);
    return value;
  }();
  static_assert(attributes.get(model::PlayerStat::physical_velocity) == 1.0f);
  static_assert(attributes.get(model::PlayerStat::technical_shortpass) == 0.8765432f);

  model::Team home;
  home.name = "Static Home";
  model::Player player;
  if (player.id != model::kInvalidPlayerId) return EXIT_FAILURE;
  player.id = 987654321u;
  player.database_id = 398;
  player.first_name = "Ada";
  player.last_name = "Lovelace";
  player.attributes = attributes;
  home.players.push_back(player);
  home.formation.push_back({{-0.5f, 0.25f}, e_PlayerRole_CM, false, true});

  model::Team copy = home;
  copy.players.front().id = 123u;
  copy.players.front().database_id = 11;
  copy.players.front().attributes.fill(0.6f);
  copy.formation.front().position.y = -0.25f;
  if (home.players.front().id != 987654321u ||
      home.players.front().database_id != 398 ||
      home.players.front().attributes.get(model::PlayerStat::technical_shortpass) !=
          0.8765432f || home.formation.front().position.y != 0.25f) {
    return EXIT_FAILURE;
  }
  for (float ability : copy.players.front().attributes.values()) {
    if (ability != 0.6f) return EXIT_FAILURE;
  }

  try {
    player.attributes.set(model::PlayerStat::player_stat_max, 0.5f);
    return EXIT_FAILURE;
  } catch (const std::out_of_range&) {
  }
  return EXIT_SUCCESS;
}
