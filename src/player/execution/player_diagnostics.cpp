// Player-side diagnostic definitions. See player_diagnostics.hpp for the
// storage scope and the rule that only entries with a real consumer remain.
#include "player/execution/player_diagnostics.hpp"

#include <stdexcept>
#include <utility>

MovementAnimationPerturbation &MovementAnimationPerturbationAudit() {
  static MovementAnimationPerturbation value;
  return value;
}

bool &ContactAuthorityAuditEnabled() {
  static bool enabled = false;
  return enabled;
}

bool IsTrackedScheduledContact(e_FunctionType type) {
  return type == e_FunctionType_Shot ||
         type == e_FunctionType_ShortPass ||
         type == e_FunctionType_LongPass ||
         type == e_FunctionType_HighPass ||
         type == e_FunctionType_Trap ||
         type == e_FunctionType_BallControl;
}

ContactAuthorityAudit &ContactAuthorityFor(e_FunctionType type) {
  static ContactAuthorityAudit records[6];
  switch (type) {
    case e_FunctionType_Shot: return records[0];
    case e_FunctionType_ShortPass: return records[1];
    case e_FunctionType_LongPass: return records[2];
    case e_FunctionType_HighPass: return records[3];
    case e_FunctionType_Trap: return records[4];
    case e_FunctionType_BallControl: return records[5];
    default:
      throw std::logic_error(
          "ContactAuthorityFor: untracked scheduled contact type");
  }
}

// ANIM-2 A11.0: body-forecast dual-run comparison counter.
int &BakedBodyPreviewChecks() { static int value = 0; return value; }
