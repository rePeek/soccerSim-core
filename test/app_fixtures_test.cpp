#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <iterator>
#include <optional>
#include <string>

#include "app/fixtures/default_teams.hpp"
#include "app/fixtures/legacy_player_profile.hpp"
#include "model/player.hpp"
#include "support/text/string_utils.hpp"

namespace {

namespace fixtures = football::app::fixtures;
namespace model = football::model;

// The shared legacy roster behind both default teams, in roster order.
constexpr model::PlayerDatabaseId kLegacyRoster[] = {
    398, 11, 254, 320, 103, 188, 74, 332, 290, 391, 264};

struct ExpectedProfile {
  model::PlayerDatabaseId database_id;
  const char* left_name;
  const char* right_name;
  int age;
  int skin_color;
  const char* hair_style;
  const char* hair_color;
  float height;
};

// Values transcribed from the legacy GRF profile table. Both sides share the
// same abilities; only the display name differs.
constexpr ExpectedProfile kExpectedProfiles[] = {
    {398, "Ada Lovelace", "Lisa Meitner", 22, 1, "long02", "blonde", 1.87f},
    {11, "Alan Turing", "Albert Einstein", 25, 2, "short02", "black", 1.7f},
    {254, "Katherine Johnson", "Dorothy Vaughaun", 30, 4, "long02", "black",
     1.74f},
    {320, "Leonardo da Vinci", "Archimedinho Archimedinho", 27, 1, "medium01",
     "black", 1.93f},
    {103, "Isaac Newton", "Stefan Banach", 31, 1, "short01", "black", 1.72f},
    {188, "David Blackwell", "Benjamin Banneker", 30, 4, "long02", "black",
     1.71f},
    {74, "Anita Borg", "Jane Goodall", 26, 3, "long02", "black", 1.89f},
    {332, "Leonhard Euler", "Nicolaus Copernicus", 26, 1, "long01", "blonde",
     1.84f},
    {290, "Pythagoras Pythagoras", "Richard Feynman", 22, 1, "medium01",
     "black", 1.74f},
    {391, "Marie Curie", "Rosalind Franklin", 27, 2, "long02", "black",
     1.81f},
    {264, "Louise Nixon Sutton", "Melba Roy Mouton", 27, 3, "short02", "black",
     1.69f},
};

std::string FullName(const model::Player& player) {
  return player.first_name + " " + player.last_name;
}

}  // namespace

TEST_CASE("legacy profile import fills the whole static description",
          "[app][fixtures]") {
  STATIC_REQUIRE(std::size(kExpectedProfiles) == std::size(kLegacyRoster));
  for (const ExpectedProfile& expected : kExpectedProfiles) {
    const model::Player left =
        fixtures::LoadLegacyPlayerProfile(expected.database_id, true);
    const model::Player right =
        fixtures::LoadLegacyPlayerProfile(expected.database_id, false);
    INFO("database id " << expected.database_id);

    CHECK(left.database_id == expected.database_id);
    CHECK(right.database_id == expected.database_id);
    CHECK(FullName(left) == expected.left_name);
    CHECK(FullName(right) == expected.right_name);
    CHECK(left.age == expected.age);
    CHECK(right.age == expected.age);
    CHECK(left.height == expected.height);
    CHECK(right.height == expected.height);
    CHECK(left.appearance.skin_color ==
          std::optional<int>{expected.skin_color});
    CHECK(right.appearance.skin_color ==
          std::optional<int>{expected.skin_color});
    CHECK(left.appearance.hair_style == expected.hair_style);
    CHECK(left.appearance.hair_color == expected.hair_color);

    // Import provenance must never invent an identity.
    CHECK(left.id == model::kInvalidPlayerId);
    CHECK(right.id == model::kInvalidPlayerId);

    // Side changes the name only, not the imported qualities.
    CHECK(left.attributes == right.attributes);
    CHECK(left.appearance.skin_color == right.appearance.skin_color);
    CHECK(left.appearance.hair_style == right.appearance.hair_style);
    CHECK(left.appearance.hair_color == right.appearance.hair_color);
    CHECK(left.height == right.height);
    CHECK(left.age == right.age);
  }
}

TEST_CASE("imported abilities are clamped and leave the neutral default",
          "[app][fixtures]") {
  for (const ExpectedProfile& expected : kExpectedProfiles) {
    const model::Player player =
        fixtures::LoadLegacyPlayerProfile(expected.database_id, true);
    INFO("database id " << expected.database_id);
    bool all_neutral = true;
    for (float ability : player.attributes.values()) {
      CHECK(ability >= 0.01f);
      CHECK(ability <= 1.0f);
      all_neutral = all_neutral && ability == 1.0f;
    }
    CHECK_FALSE(all_neutral);
  }
}

TEST_CASE("imported abilities keep the legacy six-decimal round trip",
          "[app][fixtures]") {
  for (const ExpectedProfile& expected : kExpectedProfiles) {
    const model::Player player =
        fixtures::LoadLegacyPlayerProfile(expected.database_id, true);
    INFO("database id " << expected.database_id);
    for (float ability : player.attributes.values()) {
      CHECK(ability == static_cast<float>(std::atof(
                            blunted::real_to_str(ability).c_str())));
    }
  }
}

TEST_CASE("an unknown database id stays a neutral, unnamed player",
          "[app][fixtures]") {
  const model::Player player = fixtures::LoadLegacyPlayerProfile(999999, true);
  CHECK(player.id == model::kInvalidPlayerId);
  CHECK(player.database_id == 999999);
  CHECK(player.first_name.empty());
  CHECK(player.last_name.empty());
  CHECK(player.appearance.skin_color == std::nullopt);
  CHECK(player.appearance.hair_style == "short01");
  CHECK(player.appearance.hair_color == "darkblonde");
  CHECK(player.height == 1.8f);
  CHECK(player.age == 15);
  for (float ability : player.attributes.values()) {
    CHECK(ability == 1.0f);
  }
}

TEST_CASE("default teams name and size the two sample rosters",
          "[app][fixtures]") {
  const model::Team home = fixtures::MakeDefaultHomeTeam();
  const model::Team away = fixtures::MakeDefaultAwayTeam();

  CHECK(home.name == "Frequentists United");
  CHECK(away.name == "Real Bayesians");
  REQUIRE(home.players.size() == std::size(kLegacyRoster));
  REQUIRE(away.players.size() == std::size(kLegacyRoster));

  for (std::size_t i = 0; i < std::size(kLegacyRoster); ++i) {
    INFO("roster slot " << i);
    CHECK(home.players[i].database_id == kLegacyRoster[i]);
    CHECK(away.players[i].database_id == kLegacyRoster[i]);
    CHECK(FullName(home.players[i]) == kExpectedProfiles[i].left_name);
    CHECK(FullName(away.players[i]) == kExpectedProfiles[i].right_name);
  }
}

TEST_CASE("default teams assign disjoint identities", "[app][fixtures]") {
  const model::Team home = fixtures::MakeDefaultHomeTeam();
  const model::Team away = fixtures::MakeDefaultAwayTeam();

  for (std::size_t i = 0; i < std::size(kLegacyRoster); ++i) {
    CHECK(home.players[i].id == static_cast<model::PlayerId>(i));
    CHECK(away.players[i].id ==
          static_cast<model::PlayerId>(std::size(kLegacyRoster) + i));
  }
  for (const model::Player& player : home.players) {
    CHECK(player.id != model::kInvalidPlayerId);
  }
  for (const model::Player& player : away.players) {
    CHECK(player.id != model::kInvalidPlayerId);
  }
}

TEST_CASE("default teams declare no start-position override",
          "[app][fixtures]") {
  // The fixtures rely on the normalized tactical shape; declaring an explicit
  // public-frame formation would silently change the derived kickoff state.
  CHECK(fixtures::MakeDefaultHomeTeam().formation.empty());
  CHECK(fixtures::MakeDefaultAwayTeam().formation.empty());
}

TEST_CASE("default teams carry the legacy tactical shape", "[app][fixtures]") {
  struct ExpectedEntry {
    float x;
    float y;
    e_PlayerRole role;
  };
  const ExpectedEntry expected[] = {
      {-1.0f, 0.0f, e_PlayerRole_GK},   {-0.7f, 0.75f, e_PlayerRole_LB},
      {-1.0f, 0.25f, e_PlayerRole_CB},  {-1.0f, -0.25f, e_PlayerRole_CB},
      {-0.7f, -0.75f, e_PlayerRole_RB}, {0.0f, 0.5f, e_PlayerRole_CM},
      {-0.2f, 0.0f, e_PlayerRole_CM},   {0.0f, -0.5f, e_PlayerRole_CM},
      {0.6f, 0.75f, e_PlayerRole_LM},   {1.0f, 0.0f, e_PlayerRole_CF},
      {0.6f, -0.75f, e_PlayerRole_RM}};

  const model::Team home = fixtures::MakeDefaultHomeTeam();
  const model::Team away = fixtures::MakeDefaultAwayTeam();
  REQUIRE(home.tactical_formation.size() == std::size(expected));
  REQUIRE(away.tactical_formation.size() == home.tactical_formation.size());
  for (std::size_t i = 0; i < std::size(expected); ++i) {
    INFO("formation slot " << i);
    CHECK(home.tactical_formation[i].position.x == expected[i].x);
    CHECK(home.tactical_formation[i].position.y == expected[i].y);
    CHECK(home.tactical_formation[i].role == expected[i].role);
    CHECK(away.tactical_formation[i].position.x == expected[i].x);
    CHECK(away.tactical_formation[i].position.y == expected[i].y);
    CHECK(away.tactical_formation[i].role == expected[i].role);
  }
}

TEST_CASE("default teams carry the legacy tactical preferences",
          "[app][fixtures]") {
  const model::Team home = fixtures::MakeDefaultHomeTeam();
  REQUIRE(home.tactics.size() == 12);
  CHECK(home.tactics.at("dribble_centermagnet") == 0.72f);
  CHECK(home.tactics.at("dribble_offensiveness") == 0.5f);
  CHECK(home.tactics.at("position_defense_depth_factor") == 0.3f);
  CHECK(home.tactics.at("position_defense_microfocus_strength") == 0.96f);
  CHECK(home.tactics.at("position_defense_midfieldfocus") == 0.96f);
  CHECK(home.tactics.at("position_defense_sidefocus_strength") == 0.16f);
  CHECK(home.tactics.at("position_defense_width_factor") == 0.7f);
  CHECK(home.tactics.at("position_offense_depth_factor") == 0.34f);
  CHECK(home.tactics.at("position_offense_microfocus_strength") == 0.92f);
  CHECK(home.tactics.at("position_offense_midfieldfocus") == 0.88f);
  CHECK(home.tactics.at("position_offense_sidefocus_strength") == 0.88f);
  CHECK(home.tactics.at("position_offense_width_factor") == 0.74f);
  CHECK(fixtures::MakeDefaultAwayTeam().tactics == home.tactics);
}

TEST_CASE("default teams are independent values", "[app][fixtures]") {
  model::Team home = fixtures::MakeDefaultHomeTeam();
  const model::Team original = home;
  home.players.front().attributes.fill(0.25f);
  home.players.front().id = 12345u;
  home.tactics["dribble_centermagnet"] = 0.0f;

  const model::Team fresh = fixtures::MakeDefaultHomeTeam();
  CHECK(fresh.players.front().attributes ==
        original.players.front().attributes);
  CHECK(fresh.players.front().id == original.players.front().id);
  CHECK(fresh.tactics == original.tactics);
}

TEST_CASE("the two default sides share no identity", "[app][fixtures]") {
  const model::Team home = fixtures::MakeDefaultHomeTeam();
  const model::Team away = fixtures::MakeDefaultAwayTeam();
  for (const model::Player& left : home.players) {
    for (const model::Player& right : away.players) {
      CHECK(left.id != right.id);
    }
  }
}
