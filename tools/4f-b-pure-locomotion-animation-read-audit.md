# 4f-b: pure-locomotion animation-read audit

Scope: real Player locomotion only; Shot/Pass/Trap/BallControl contact and animation presentation are **not** Movement authority. Inventory at 4f-b3, following 4f-b1 (`070901b`) and 4f-b2 (`71c93e6`). No golden update for b1–b3.

Current-tree naming: the `PlayerBase` methods in this historical inventory now
live directly on `Player`; the base class and `playerbase.*` are deleted, without
changing the audited locomotion/contact authority boundary.

## Searches

```sh
rg -n 'kinematicShadow|GetKinematicShadow|UpdateKinematicShadow|ResetKinematicShadow' src tools
rg -n 'originatingCommand|GetOriginatingCommand|GetCurrentAnim\(' src tools
rg -n 'ProjectMovementState|UsesProceduralLocomotion|BuildLegacyLocomotionInput|PlayerLocomotion::Step' src
rg -n 'GetFrameNum\(|GetFrameCount\(|animMovement|GetOutgoingFoot' src/sim/player
```

The first search has no results. No remaining source or regression read of `originatingCommand.useDesiredMovement` gates the locomotion/reentry or intercept audits. The reset/retain command-provenance counters have also been deleted: they were diagnostic reads of animation-carried Movement values, not controller or intent authority.

## Ownership and residual reads

- `HumanoidBase::ProjectMovementState` (`humanoidbase.cpp`) is the pure-locomotion execution site. `UsesProceduralLocomotion()` requires simulation action eligibility and a current-epoch executable decision intent; `CheckDecisionLocomotionIntentOracle()` verifies the producer. `BuildLegacyLocomotionInput` receives **`GetDecisionLocomotionIntent()` and tick-start `PlayerKinematicState`**, not an animation command; `PlayerLocomotion::Step` and `PlayerBodyFacing::Step` update the projected kinematic state. The adapter's *name* is historical, not a source of animation input.
- `HumanoidBase::CalculateSpatialState` returns early on the procedural path, bypassing animation root motion/body pose. Its remaining `currentAnim` read updates `spatialState.foot` (gait/selection); `ApplySimulationMovementState` projects simulation velocity into `actualMovement`, `physicsMovement`, and `animMovement`. The non-procedural path still uses animation root motion by design.
- `Humanoid::Process` increments the animation frame and runs `SelectAnim` only for an animation opportunity; this selection consumes the serialized Player Decision queue (or Trip-local queue). Locomotion cadence separately publishes cached Movement, and continuity repair forces a fresh Player Decision query before execution. `Humanoid::SelectAnim` compares the *current* animation's originating command with a candidate for presentation/requeue selection; `RecordSchedulerQuery` compares them for diagnostics. Neither is the command passed to `PlayerLocomotion::Step`.
- `HumanoidBase::SelectAnim` reads animation attributes for matching, foot choice and candidate prediction. `CalculateFactualSpatialState` is called during selection and can project animation special-state/foot fields into `spatialState` before later projection; it is **not** certified perturbation-independent by this static audit. The 4f-c A/B must check whether selection-side changes can indirectly affect later gameplay (including action transitions). Do not equate a remaining animation read with a direct Movement-intent read, or claim strict A/B equivalence yet.
- `PlayerBase::CaptureLegacyActionState` / `BeginSimulationAction` read animation frames as the action-state oracle/bootstrap; the action/contact lifecycle remains in scope for a later migration, not Movement cleanup. `GetKinematicState`/`GetMovement`/`GetDirectionVec` on `PlayerBase` return the simulation kinematic mirror. `PlayerBase::SynchronizeKinematicState` reads the Humanoid's **projected** spatial state and checks it via `CheckSimulationKinematicOracle`, not the originating command.
- `humanoid_utils.cpp` reads `currentAnim.originatingCommand` in `GetBallControlVector` and `GetShotVector`; `humanoid.cpp` reads its modifiers/touchInfo for contact. These are retained action/contact semantics. The corresponding `tools/football_regression.cpp` reads of `GetCurrentAnim()` are legacy-versus-procedural diagnostic comparisons, not production locomotion authority; removing those measurements is not required for 4f-b.
- `HumanoidBase::GetCurrentAnim` and `PlayerBase::GetCurrentAnim` remain for action and diagnostic consumers. The uncalled `GetOriginatingCommand()` accessor and reset/retain provenance-only reads were removed in 4f-b3. `Anim::originatingCommand` itself remains serialized for presentation and contact; deleting or rewriting it would change action behavior and schema.

## Boundary

Static inventory closes the **direct pure-locomotion animation-command read** cleanup, not the question whether changes to animation selection can indirectly perturb gameplay. 4f-c must compare baseline and perturbed animation lifecycle runs without invoking mutating `SelectAnim` twice, distinguish proven equivalence from unproven cases, and attribute any first divergence. Contact/action behavior and gameplay effects from animation selection are not presumed equivalent.
