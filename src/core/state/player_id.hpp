#ifndef _HPP_CORE_STATE_PLAYER_ID
#define _HPP_CORE_STATE_PLAYER_ID

#include <cstdint>

// Simulation-level player identity. This is NOT a team array slot: trades,
// substitutions, mirroring and serialization layout must not change it. The
// first production value comes from the legacy stable_id; Match-side lookups
// (team, slot, Player facade) happen only at the boundary, never inside
// physics.
using PlayerId = uint32_t;
constexpr PlayerId kInvalidPlayerId = UINT32_MAX;

#endif  // _HPP_CORE_STATE_PLAYER_ID