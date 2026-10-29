#ifndef FOOTBALL_SIM_PLAYER_PLAYER_DEFINITION_HPP
#define FOOTBALL_SIM_PLAYER_PLAYER_DEFINITION_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

namespace football::player {

// Caller-provided identity, independent of roster order and match indexing.
using PlayerId = std::uint32_t;
inline constexpr PlayerId kInvalidPlayerId = std::numeric_limits<PlayerId>::max();

// External data-source key for tracing where this definition came from.
// Provenance only: two definitions that share it are not the same player.
using PlayerDatabaseId = int;

// Stable ordering of the base ability items. They describe what a player can do
// in the abstract - not current fatigue, actions or any other mutable match
// state (see Player::GetStat for the value actually in force).
enum class PlayerStat {
  physical_balance,
  physical_reaction,
  physical_acceleration,
  physical_velocity,
  physical_stamina,
  physical_agility,
  physical_shotpower,
  technical_standingtackle,
  technical_slidingtackle,
  technical_ballcontrol,
  technical_dribble,
  technical_shortpass,
  technical_highpass,
  technical_header,
  technical_shot,
  technical_volley,
  mental_calmness,
  mental_workrate,
  mental_resilience,
  mental_defensivepositioning,
  mental_offensivepositioning,
  mental_vision,
  player_stat_max
};

inline constexpr std::size_t kPlayerStatCount =
    static_cast<std::size_t>(PlayerStat::player_stat_max);

class PlayerAttributes {
 public:
  constexpr PlayerAttributes() { values_.fill(1.0f); }

  constexpr float get(PlayerStat stat) const {
    return values_.at(static_cast<std::size_t>(stat));
  }
  constexpr void set(PlayerStat stat, float value) {
    values_.at(static_cast<std::size_t>(stat)) = value;
  }
  constexpr void fill(float value) { values_.fill(value); }
  constexpr const std::array<float, kPlayerStatCount>& values() const {
    return values_;
  }
  bool operator==(const PlayerAttributes&) const = default;

 private:
  std::array<float, kPlayerStatCount> values_;
};

struct PlayerAppearance {
  // Optional: absent means the renderer derives a colour from the player's
  // identity, a present value is authoritative. Constructing a model never
  // draws RNG.
  std::optional<int> skin_color;
  std::string hair_style = "short01";
  std::string hair_color = "darkblonde";
};

// Complete static player description. Database identity is provenance only:
// the runtime consumes these values rather than reloading them from the key.
struct PlayerDefinition {
  PlayerId id = kInvalidPlayerId;
  PlayerDatabaseId database_id = 0;
  std::string first_name;
  std::string last_name;
  int age = 15;
  float height = 1.8f;
  // Body mass in kilograms, the anthropometric input that sets body girth.
  // Optional: a definition may carry a mass, or describe only its height, in
  // which case the body profile uses the reference mass for that height
  // (girth 1.0). A present value must be finite and positive; see
  // MakePlayerBodyProfile.
  std::optional<float> weight_kg;
  PlayerAttributes attributes;
  PlayerAppearance appearance;
};

}  // namespace football::player

#endif  // FOOTBALL_SIM_PLAYER_PLAYER_DEFINITION_HPP
