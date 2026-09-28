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
