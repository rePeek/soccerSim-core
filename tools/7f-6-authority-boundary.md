# 7F-6 player self-movement authority boundary

7F owns how a single player produces kinematics. On a normal tick, procedural
locomotion or `LegacyAnimationKinematics::Evaluate()` produces a
`PlayerKinematicResult`; `PlayerBase::ApplyKinematicResult()` writes PlayerState,
then Humanoid projects its four gameplay fields to SpatialState. Tick-end code
checks `CheckSimulationKinematicOracle()` instead of copying back.

Reset/activation are state discontinuities, not collision corrections.
`LegacyResetKinematics::Evaluate(position, focus)` retains the legacy start
angle **separately** from the four-field result, including the historical
canonical world-space body facing. PlayerBase applies the result before
Humanoid resets animation selection and compatibility fields. Its constructor
uses the same reset evaluation before PlayerBase has installed the Humanoid;
activation applies the authoritative result afterward. `ResetSituation` keeps
its continuity-epoch and action-schedule behavior. The bit-exact oracle checks
both reset entry points.

**Deferred to 7G:** `Match` collision resolution and
`PlayerBase::OffsetPosition()` / `HumanoidBase::OffsetPosition()` still use the
legacy SpatialState → PlayerState synchronizer. A trial replacing this with
`kinematicState.position += offset` followed by projection changed the
regression trajectory (`football_regression` first reported ball[0] at call 100:
expected 0.582221, got 0.587306). This is not a self-movement producer;
7F must not add collision compatibility hacks to make it state-first.

Save/restore still serializes Humanoid spatial state, PlayerState and collider
and checks their bit-exact consistency. Do not change that format in 7F.
Acceptance: release build and all CTest regression/animation A/B/headless tests
pass without baseline regeneration.

7F self-movement authority is complete. 7G-0 separately moves ownership of
the first 11 players on each team to `WorldState.players`, without changing
collision resolution or the legacy serialization stream. Officials and any
extra bench players still have local movement state. Substitution/rebinding
of an extra bench player into a World slot is not part of 7G-0; it needs an
explicit policy before contact prediction assumes all active players are
World-owned. The current player and ball domain entities live in
`football::model`; legacy `::Player` and `BallLegacy` remain facades.
`PlayerKinematics` is experimental/reference only; runtime movement is
`PlayerLocomotion` + `PlayerBodyFacing`. PlayerState and PlayerKinematicResult
use `movementFacing` for locomotion direction and `torsoFacing` for torso
orientation; serialized field order and legacy SpatialState naming are unchanged.
