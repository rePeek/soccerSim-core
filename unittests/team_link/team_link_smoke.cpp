// Independent Team boundary smoke test.
//
// The executable includes only the public include tree and force-links every
// football_team object. Team's declared closure is player/foundation/pitch plus
// Player's lower layers; it must never need a Runtime, Referee, Event or AI
// symbol to link.
#include "football/team/team.hpp"

int main() {
  football::team::Team team;
  return team.ActivePlayerCount() == 0 && team.Goalie() == nullptr ? 0 : 1;
}
