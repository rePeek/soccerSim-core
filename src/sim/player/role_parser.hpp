#ifndef FOOTBALL_SIM_PLAYER_ROLE_PARSER_HPP
#define FOOTBALL_SIM_PLAYER_ROLE_PARSER_HPP

#include <string>

#include "sim/player/player_role.hpp"

// Parses a role token ("GK", "CB", ...) from external configuration. Unknown
// tokens fall back to CM. This is import/lookup, not player behaviour.
e_PlayerRole GetRoleFromString(const std::string& roleString);

#endif  // FOOTBALL_SIM_PLAYER_ROLE_PARSER_HPP
