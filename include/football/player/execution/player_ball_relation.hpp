// One actor's relation to the ball for the current tick.
//
// This is Runtime-owned derived state, not Player state: it is a relation
// between the actor, the ball and the other 21 actors. Runtime computes every
// instance for all players and exposes them through borrowed views; the Player
// stores none of it.
//
// The estimate is refreshed on a staggered 100 ms cadence per actor (one tick
// in ten), so `time_to_ball_ms` is retained between refreshes.
#ifndef FOOTBALL_PLAYER_PLAYER_BALL_RELATION_HPP
#define FOOTBALL_PLAYER_PLAYER_BALL_RELATION_HPP

namespace football::player {

struct PlayerBallRelation {
  // Interception ETA estimates in milliseconds.
  int time_to_ball_ms = 1000;
  int optimistic_ms = 1000;
  // Previous tick's usual estimate, kept for the ETA sensitivity calculations.
  int previous_time_to_ball_ms = 1000;
  // The actor could reach the ball; team-local and match-wide dominance.
  bool locally_controllable = false;
  bool best_in_team = false;
  bool unique_in_match = false;
};

}  // namespace football::player

#endif  // FOOTBALL_PLAYER_PLAYER_BALL_RELATION_HPP
