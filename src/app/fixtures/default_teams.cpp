#include "app/fixtures/default_teams.hpp"
#include "app/fixtures/legacy_player_profile.hpp"

#include <iterator>
#include <utility>

namespace football::app::fixtures {
namespace {

model::Team MakeDefaultTeam(const std::string& name, bool left_team) {
  model::Team team;
  team.name = name;
  constexpr model::PlayerDatabaseId player_ids[] = {398, 11, 254, 320, 103, 188,
                                                    74, 332, 290, 391, 264};
  // Identities belong to the caller's two distinct rosters, not database keys.
  model::PlayerId player_id = left_team ? 0 : std::size(player_ids);
  for (model::PlayerDatabaseId database_id : player_ids) {
    model::Player player = LoadLegacyPlayerProfile(database_id, left_team);
    player.id = player_id++;
    team.players.push_back(std::move(player));
  }
  // The old embedded team XML is now typed sample input owned by this app.
  team.tactical_formation = {
      {{-1.0f, 0.0f}, e_PlayerRole_GK}, {{-0.7f, 0.75f}, e_PlayerRole_LB},
      {{-1.0f, 0.25f}, e_PlayerRole_CB}, {{-1.0f, -0.25f}, e_PlayerRole_CB},
      {{-0.7f, -0.75f}, e_PlayerRole_RB}, {{0.0f, 0.5f}, e_PlayerRole_CM},
      {{-0.2f, 0.0f}, e_PlayerRole_CM}, {{0.0f, -0.5f}, e_PlayerRole_CM},
      {{0.6f, 0.75f}, e_PlayerRole_LM}, {{1.0f, 0.0f}, e_PlayerRole_CF},
      {{0.6f, -0.75f}, e_PlayerRole_RM}};
  team.tactics = {
      {"dribble_centermagnet", 0.72f}, {"dribble_offensiveness", 0.5f},
      {"position_defense_depth_factor", 0.3f},
      {"position_defense_microfocus_strength", 0.96f},
      {"position_defense_midfieldfocus", 0.96f},
      {"position_defense_sidefocus_strength", 0.16f},
      {"position_defense_width_factor", 0.7f},
      {"position_offense_depth_factor", 0.34f},
      {"position_offense_microfocus_strength", 0.92f},
      {"position_offense_midfieldfocus", 0.88f},
      {"position_offense_sidefocus_strength", 0.88f},
      {"position_offense_width_factor", 0.74f}};
  return team;
}

}  // namespace

model::Team MakeDefaultHomeTeam() {
  return MakeDefaultTeam("Frequentists United", true);
}

model::Team MakeDefaultAwayTeam() {
  return MakeDefaultTeam("Real Bayesians", false);
}

}  // namespace football::app::fixtures
