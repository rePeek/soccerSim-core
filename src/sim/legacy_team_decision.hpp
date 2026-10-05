#ifndef FOOTBALL_SIM_LEGACY_TEAM_DECISION_HPP
#define FOOTBALL_SIM_LEGACY_TEAM_DECISION_HPP

#include "sim/gamedefines.hpp"

class Player;
class Team;

// TRANSITIONAL SEAM, not the final architecture. This is only the surface the
// simulation currently calls on whoever plans a team's play, so the dependency
// arrow can be flipped (sim -> abstract port, ai -> implementation) without
// changing behaviour.
//
// It is deliberately fat and deliberately legacy-named: several of these are
// simulation match state (set-piece taker, attacking run / team pressure
// timers) that leaked into the decision owner. They are expected to move back
// into match/rules state in a later pass, after which this port shrinks. Do not
// treat it as a public API and do not grow it beyond what simulation calls.
class LegacyTeamDecision {
 public:
  virtual ~LegacyTeamDecision() = default;

  virtual void Process() = 0;

  virtual Vector3 GetAdaptedFormationPosition(
      Player *player, bool use_dynamic_formation_position = true) = 0;
  virtual void CalculateDynamicRoles() = 0;
  virtual void CalculateManMarking() = 0;
  virtual void ApplyOffsideTrap(Vector3 &position) const = 0;
  virtual float GetOffsideTrapX() const = 0;

  // Legacy set-piece state that still lives on the decision owner.
  virtual void PrepareSetPiece(e_GameMode set_piece, Team *other_team,
                               int kickoff_taker_team_id, int taker_team_id) = 0;
  virtual Player *GetPieceTaker() = 0;
  virtual e_GameMode GetSetPieceType() = 0;

  // Legacy tactical timers that still live on the decision owner.
  virtual void ApplyAttackingRun(Player *manual_player = 0) = 0;
  virtual void ApplyTeamPressure() = 0;
  virtual void ApplyKeeperRush() = 0;
  virtual unsigned long GetEndApplyAttackingRun_ms() = 0;
  virtual Player *GetAttackingRunPlayer() = 0;
  virtual unsigned long GetEndApplyTeamPressure_ms() = 0;
  virtual Player *GetTeamPressurePlayer() = 0;
  virtual Player *GetForwardSupportPlayer() = 0;
  virtual unsigned long GetEndApplyKeeperRush_ms() = 0;

  virtual void UpdateTactics() = 0;
  virtual void Reset() = 0;
};

#endif  // FOOTBALL_SIM_LEGACY_TEAM_DECISION_HPP
