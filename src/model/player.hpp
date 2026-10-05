#ifndef FOOTBALL_MODEL_PLAYER_HPP
#define FOOTBALL_MODEL_PLAYER_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

namespace football::model {

// Caller-provided identity, independent of roster order and match indexing.
using PlayerId = std::uint32_t;
inline constexpr PlayerId kInvalidPlayerId = std::numeric_limits<PlayerId>::max();

// Legacy GRF database profile key: import provenance, not player identity.
using PlayerDatabaseId = int;

// Stable ordering of the legacy ability items. These describe base abilities,
// not current fatigue, actions or other mutable match state.
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
  // Missing skin colour lets the legacy runtime adapter select it. Known
  // profiles carry an explicit colour; constructing a model never draws RNG.
  std::optional<int> skin_color;
  std::string hair_style = "short01";
  std::string hair_color = "darkblonde";
};

// Complete static player description. Database identity is provenance only:
// the runtime consumes these values rather than reloading them from the key.
struct Player {
  PlayerId id = kInvalidPlayerId;
  PlayerDatabaseId database_id = 0;
  std::string first_name;
  std::string last_name;
  int age = 15;
  float height = 1.8f;
  PlayerAttributes attributes;
  PlayerAppearance appearance;
};

}  // namespace football::model

#endif  // FOOTBALL_MODEL_PLAYER_HPP
