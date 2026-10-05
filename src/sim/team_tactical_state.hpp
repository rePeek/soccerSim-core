#ifndef FOOTBALL_SIM_TEAM_TACTICAL_STATE_HPP
#define FOOTBALL_SIM_TEAM_TACTICAL_STATE_HPP

class Player;

// Authoritative lifetimes of human/team control requests. Owned/reset by Team,
// not AI. Selection is a query/control result; deadlines are simulation time.
struct TeamTacticalState {
  unsigned long attacking_run_until_ms = 0;
  Player *attacking_runner = nullptr;
  unsigned long pressure_until_ms = 0;
  Player *pressure_player = nullptr;
  unsigned long keeper_rush_until_ms = 0;
};

#endif
