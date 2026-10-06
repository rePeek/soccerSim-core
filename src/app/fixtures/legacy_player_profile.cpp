// Copyright 2019 Google LLC & Bastiaan Konings
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "app/fixtures/legacy_player_profile.hpp"
#include "foundation/math/scalar.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace football::app::fixtures {
namespace {

struct Name {
  const char* first;
  const char* last;
};
struct Profile {
  model::PlayerDatabaseId database_id;
  Name home_name;
  Name away_name;
  float base_stat;
  int age;
  int skin_color;
  const char* hair_style;
  const char* hair_color;
  float height;
  // Stable model::PlayerStat ordering, not strings or XML at runtime.
  std::array<float, model::kPlayerStatCount> stats;
};
static_assert(model::kPlayerStatCount == 22);

constexpr Profile kProfiles[] = {
    {398, {"Ada", "Lovelace"}, {"Lisa", "Meitner"},
     0.661913f, 22, 1, "long02", "blonde", 1.87f,
     {0.650000f, 0.850000f, 0.550000f, 0.450000f, 0.550000f, 0.650000f, 0.650000f,
      0.250000f, 0.250000f, 0.350000f, 0.250000f, 0.650000f, 0.750000f, 0.250000f,
      0.350000f, 0.250000f, 0.550000f, 0.550000f, 0.550000f, 0.950000f, 0.150000f, 0.550000f}},
    {11, {"Alan", "Turing"}, {"Albert", "Einstein"},
     0.619111f, 25, 2, "short02", "black", 1.7f,
     {0.569697f, 0.536364f, 0.536364f, 0.536364f, 0.469697f, 0.536364f, 0.569697f,
      0.536364f, 0.536364f, 0.503030f, 0.469697f, 0.569697f, 0.536364f, 0.469697f,
      0.436364f, 0.303030f, 0.469697f, 0.469697f, 0.469697f, 0.536364f, 0.436364f, 0.503030f}},
    {254, {"Katherine", "Johnson"}, {"Dorothy", "Vaughaun"},
     0.64269f, 30, 4, "long02", "black", 1.74f,
     {0.713636f, 0.613636f, 0.463636f, 0.463636f, 0.463636f, 0.463636f, 0.563636f,
      0.713636f, 0.663636f, 0.463636f, 0.313636f, 0.563636f, 0.563636f, 0.513636f,
      0.313636f, 0.213636f, 0.463636f, 0.463636f, 0.463636f, 0.663636f, 0.313636f, 0.563636f}},
    {320, {"Leonardo", "da Vinci"}, {"Archimedinho", "Archimedinho"},
     0.625396f, 27, 1, "medium01", "black", 1.93f,
     {0.736364f, 0.686364f, 0.486364f, 0.486364f, 0.486364f, 0.436364f, 0.636364f,
      0.786364f, 0.786364f, 0.386364f, 0.236364f, 0.436364f, 0.436364f, 0.586364f,
      0.236364f, 0.186364f, 0.486364f, 0.486364f, 0.486364f, 0.786364f, 0.236364f, 0.486364f}},
    {103, {"Isaac", "Newton"}, {"Stefan", "Banach"},
     0.60786f, 31, 1, "short01", "black", 1.72f,
     {0.562121f, 0.528788f, 0.528788f, 0.528788f, 0.462121f, 0.495455f, 0.528788f,
      0.595455f, 0.562121f, 0.528788f, 0.428788f, 0.628788f, 0.595455f, 0.462121f,
      0.362121f, 0.262121f, 0.462121f, 0.462121f, 0.462121f, 0.595455f, 0.362121f, 0.595455f}},
    {188, {"David", "Blackwell"}, {"Benjamin", "Banneker"},
     0.68319f, 30, 4, "long02", "black", 1.71f,
     {0.386364f, 0.486364f, 0.586364f, 0.586364f, 0.486364f, 0.686364f, 0.686364f,
      0.186364f, 0.186364f, 0.586364f, 0.686364f, 0.586364f, 0.486364f, 0.386364f,
      0.686364f, 0.486364f, 0.486364f, 0.486364f, 0.486364f, 0.286364f, 0.686364f, 0.386364f}},
    {74, {"Anita", "Borg"}, {"Jane", "Goodall"},
     0.645507f, 26, 3, "long02", "black", 1.89f,
     {0.663636f, 0.563636f, 0.463636f, 0.463636f, 0.463636f, 0.463636f, 0.563636f,
      0.663636f, 0.563636f, 0.463636f, 0.363636f, 0.663636f, 0.663636f, 0.463636f,
      0.363636f, 0.263636f, 0.463636f, 0.463636f, 0.463636f, 0.563636f, 0.363636f, 0.563636f}},
    {332, {"Leonhard", "Euler"}, {"Nicolaus", "Copernicus"},
     0.63531f, 26, 1, "long01", "blonde", 1.84f,
     {0.525000f, 0.525000f, 0.525000f, 0.525000f, 0.475000f, 0.575000f, 0.625000f,
      0.425000f, 0.375000f, 0.525000f, 0.525000f, 0.625000f, 0.575000f, 0.425000f,
      0.525000f, 0.375000f, 0.475000f, 0.475000f, 0.475000f, 0.425000f, 0.525000f, 0.475000f}},
    {290, {"Pythagoras", "Pythagoras"}, {"Richard", "Feynman"},
     0.716847f, 22, 1, "medium01", "black", 1.74f,
     {0.381818f, 0.481818f, 0.631818f, 0.631818f, 0.481818f, 0.681818f, 0.681818f,
      0.181818f, 0.131818f, 0.531818f, 0.731818f, 0.581818f, 0.531818f, 0.381818f,
      0.681818f, 0.431818f, 0.481818f, 0.481818f, 0.481818f, 0.256818f, 0.706818f, 0.431818f}},
    {391, {"Marie", "Curie"}, {"Rosalind", "Franklin"},
     0.693097f, 27, 2, "long02", "black", 1.81f,
     {0.381818f, 0.481818f, 0.631818f, 0.631818f, 0.481818f, 0.681818f, 0.681818f,
      0.181818f, 0.131818f, 0.531818f, 0.731818f, 0.581818f, 0.531818f, 0.381818f,
      0.681818f, 0.431818f, 0.481818f, 0.481818f, 0.481818f, 0.256818f, 0.706818f, 0.431818f}},
    {264, {"Louise", "Nixon Sutton"}, {"Melba", "Roy Mouton"},
     0.718346f, 27, 3, "short02", "black", 1.69f,
     {0.381818f, 0.481818f, 0.631818f, 0.631818f, 0.481818f, 0.681818f, 0.681818f,
      0.181818f, 0.131818f, 0.531818f, 0.731818f, 0.581818f, 0.531818f, 0.381818f,
      0.681818f, 0.431818f, 0.481818f, 0.481818f, 0.481818f, 0.256818f, 0.706818f, 0.431818f}},
};

// Preserve both the legacy age formula and the %f -> atof quantization. This
// sample-data policy is intentionally local to fixtures, not a text utility.
float CalculateLegacyStat(float baseStat, float profileStat, float age) {
  using namespace blunted;
  float idealAge = 27;
  float ageFactor = curve(1.0f - NormalizedClamp(fabs(age - idealAge), 0, 13) * 0.5f,
                          1.0f) * 2.0f - 1.0f;
  assert(ageFactor >= 0.0f && ageFactor <= 1.0f);
  float agedBaseStat = baseStat * (ageFactor * 0.5f + 0.5f) * 1.2f;
  const float value = clamp(profileStat * 2.0f * agedBaseStat, 0.01f, 1.0f);
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%f", value);
  return static_cast<float>(std::atof(buffer));
}

}  // namespace

model::Player LoadLegacyPlayerProfile(model::PlayerDatabaseId database_id,
                                     bool left_team) {
  model::Player player;
  player.database_id = database_id;
  for (const Profile& profile : kProfiles) {
    if (profile.database_id != database_id) continue;
    const Name name = left_team ? profile.home_name : profile.away_name;
    player.first_name = name.first;
    player.last_name = name.last;
    player.age = profile.age;
    player.height = profile.height;
    player.appearance.skin_color = profile.skin_color;
    player.appearance.hair_style = profile.hair_style;
    player.appearance.hair_color = profile.hair_color;
    for (std::size_t i = 0; i < profile.stats.size(); ++i) {
      player.attributes.set(static_cast<model::PlayerStat>(i),
                            CalculateLegacyStat(profile.base_stat,
                                                profile.stats[i], profile.age));
    }
    break;
  }
  return player;
}

}  // namespace football::app::fixtures
