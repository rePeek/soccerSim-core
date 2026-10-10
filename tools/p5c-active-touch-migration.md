# P5c active-touch production migration

Scope: replace the legacy `ApplyBallTouch + SetRotation` active contact with a
`BallImpulse` fed to the single authoritative `Ball::Step`, and derive spin from
the contact point instead of assigning angular velocity. This note is the P5c-1
dependency analysis plus the accepted P5c-2 model. Production is **not** switched
by these stages.

## P5c-1: current execution dependencies

`Simulation::StepImpl` currently runs, in order:

1. pending change of ends / executed-tick count / frame-local controls;
2. `Mirror(reverse, !reverse, false)` — ball is in the first roster frame;
3. `BeginBodyCollisionShadow` (read-only, opt-in);
4. `ResolveBallPlayerContacts` — the legacy passive body/player resolver; it can
   call `ApplyBallTouch` and draw the three `rng_.Uniform(-30, 30)` values;
5. `ProcessReferee` (period/out-of-play/foul);
6. `Mirror(false, false, reverse)` then `Ball::Step(TickSpan{1})` — the one
   authoritative ball tick; `previous_ball_pos` is sampled before it;
7. `CaptureMentalImage`;
8. `step_team(first)` then `step_team(second)` — each actor's
   `Player::Process` → `Humanoid::Process`, with the ball in that roster's frame;
   `MeasureBodyShadowEndpoint` after each real Process;
9. possession refresh/arbitration, `ResolvePlayerContacts` + `AssessFoul`;
10. clock advance, recognizer, goal checks, `CompleteBodyCollisionShadow`,
    pending rulings, snapshot.

`Humanoid::Process` is the only producer of a real active contact. Inside it:

| Stage | What it does | Randomness |
|---|---|---|
| mental-image sample, decayed difficulty | history sampling, `decayingPositionOffset *= 0.95` | none |
| decision clock / controller query | `RequestCommand`, decision queue | none (control is input) |
| `CalculateSpatialState` / `ProjectMovementState` | authoritative kinematic state | none |
| `currentAnim.frameNum++`, `StepSimulationAction` | animation/action frame advance | none |
| requeue/selection (`SelectAnim`) | animation choice | none |
| controlled-collision trigger and `GetTrapVector` | touch preparation | **yes** (trap vector uses `rng_`) |
| scheduled contact frame: `GetTrapVector` / `GetBallControlVector` / `GetPass` / `GetShotVector` / Interfere / Deflect / Sliding | touch vector | **yes** (trap/shot/deflect/interfere draw `rng_`) |
| `ApplyBallTouch` | `Touch` → newest history refresh → possession refresh | none |
| `SetRotation` | direct angular-velocity assignment | none |
| `notify_touch` | synchronous `AcceptedTouchSink` → recognizer → referee | none |
| retain anchor/release | position/velocity constraint | none |

Key facts for the migration:

- The touch vector and the resulting angular velocity are produced **after**
  `Ball::Step` in the same tick. `ApplyBallTouch + SetRotation` therefore take
  effect as the *next* tick's initial state. An endpoint impulse applied at the
  end of `Ball::Step` has exactly the same observable timing, so P5c-1 does not
  need to delay anything by a tick; it needs the candidate to exist *before*
  `Ball::Step`.
- The candidate inputs depend on state that only advances inside
  `Humanoid::Process` (animation frame, `StepSimulationAction`, `spatialState`).
  Running a second `Humanoid::Process` to obtain a candidate is forbidden: it
  would consume RNG and advance the animation twice.
- Therefore P5c-1 must split `Humanoid::Process` into a *prepare* phase
  (motion, animation/action frame, touch vector, candidate emission) and a
  *commit* phase (`ApplyBallTouch`/impulse, retain anchor, notification,
  smuggle), both executed once per actor per tick. The prepare phase for all
  active actors must run before the single `Ball::Step`; commit runs after.
- The prepare phase reads ball-derived facts (`Predict(0)`, `Predict(100)`,
  `Predict(200)`, `GetMovement`, touch biases). Moving it before `Ball::Step`
  changes which ball sample those reads see. That is the main golden-affecting
  change and must be a separately measured step.
- The `AcceptedTouchSink` is synchronous; recognizer and referee run inside the
  producer call. Splitting prepare/commit must keep one notification per accepted
  touch, in the same relative order, or those consumers must move to the commit
  phase explicitly.
- `ResolveBallPlayerContacts` (step 4) legitimately draws RNG and can publish a
  touch; a migrated active candidate must not duplicate it.

### Reconciliation measurement (implemented, read-only)

The active-touch shadow now also runs the pure P5c-2 model on the same captured
inputs and compares its reach decision with the legacy contact-frame gate. On
seed 42, 4000 steps, both processing orders:

| order | checks | disagreements | max reach-error gap |
|---|---|---|---|
| normal | 21 | 1 | 0 m |
| reverse | 17 | 1 | 0 m |

The distance source agrees exactly (`gap = 0`); the residual decision difference
is the **height** gate: the legacy gate reads `Ball::Predict(0)` (the prediction
cache), while the pure model reads the authoritative endpoint state. Unifying
that source is the first concrete P5c-1 task; it is the reason the pure model
cannot yet replace the legacy gate.

## P5c-2: contact point and spin model (accepted, not yet wired)

`player/player_active_touch_model.hpp` implements the agreed relation
`(P, J) -> (Δv, Δω)` with no angular-velocity assignment:

- Reachability is evaluated **before** any projection: aim direction, then
  `|desired_ball_center - ball.position| < reach`, then the height gap. Failure
  returns an explicit `TouchRejection` and no impulse; a surface projection can
  never hide an out-of-reach action.
- A technique offset beyond the ball silhouette is rejected (`SurfaceRange`).
- `J = mass * (target_velocity - passive_endpoint.velocity)`.
- The contact normal is the direction of `J`, so a zero-offset strike is exactly
  torque-free even for a moving incoming ball; the contact point is the near-side
  surface point plus a lateral/vertical technique offset, projected onto the
  sphere.
- `ApplyImpulseAtPoint` produces the linear response and the spin. Sidespin comes
  from a lateral offset, top/back spin from a vertical offset, and left/right
  mirror maps to mirrored spin.

Measured: centre strike spin `< 1e-6`; inside/outside foot give opposite
sidespin; vertical offsets give opposite top/back spin; the linear response
always equals the requested target velocity (`velocity_error < 1e-5`); mirrored
problems give mirrored spin; Magnus deviation grows monotonically over 0.5 s /
1 s / 2 s and stays finite at `spin > 1 rad/s` (production
`magnus_coefficient = 0.02`).

Old trajectories are a style reference only: the legacy `SetRotation` values
(up to ~446 rad/s) are not reproducible by a point impulse and are **not** a
bit-exact target.

## Remaining P5c work

- P5c-1: split `Humanoid::Process` into prepare/commit, move prepare before the
  single `Ball::Step`, unify the ball position source, and keep one touch event
  and one RNG consumption per actor. This is a behavior-affecting change and must
  be measured with a saved baseline plus an explained diff.
- P5c-3: enable the arbitrated endpoint impulse for Shot/Pass/Trap first, under a
  separate switch, with old-baseline reconciliation; then BallControl,
  Interfere, Deflect, Sliding, controlled collision and retain separately.
- P4d-2, P6, P7 remain as previously described.

### Flight-deviation calibration (implemented, shadow only)

The active-touch shadow now measures how far the physical centre-strike response
flies from the legacy post-touch state, using the production kernel
(`Ball::Predict` with the same `BallEnvironment{}`), at 0.5 s / 1 s / 2 s.
Largest deviation per action, seed 42, 4000 steps, metres:

| action | order | samples | 0.5 s | 1 s | 2 s |
|---|---|---|---|---|---|
| movement | normal | 2 | 0.17 | 1.17 | 2.74 |
| movement | reverse | 3 | 0.54 | 2.00 | 5.91 |
| ball_control | normal | 1 | 0.00 | ~0 | ~0 |
| short_pass | normal | 10 | 8.87 | 15.91 | 21.58 |
| short_pass | reverse | 9 | 6.55 | 13.54 | 11.34 |
| shot | reverse | 2 | 0.00 | 0.00 | 0.00 |

A centre strike (zero technique offset) has no spin, so this difference is the
flight effect of the legacy direct spin, not an error of the impulse response.
It quantifies why the legacy `SetRotation` values cannot be carried over and why
the technique offsets must be calibrated before the switch is enabled: an
uncalibrated centre strike changes a 2 s pass trajectory by up to ~22 m. Actions
whose legacy spin was near zero (shot reverse, ball_control) already agree.

## P5c-3: production contact-point takeover (implemented, opt-in)

`Simulation::EnableActiveImpulseProduction(true)` switches the four migrated
strike actions (Shot, ShortPass, LongPass, HighPass, Trap) from
`Ball::Touch + Ball::SetRotation` to one physical point impulse:

- `ApplyBallImpulse` (ball_touch_application) keeps the legacy refresh order
  (newest mental-image predictions, then both possession refreshes) but changes
  the ball with `Ball::ApplyContactImpulse`: the legacy resting-height clamp,
  then `ApplyImpulseAtPoint`, then prediction rebuild with the live environment.
- `Humanoid` routes the migrated actions through the P5c-2 proposal with a
  provisional technique table (Shot `{0, .015}`, HighPass `{.03, .010}`,
  LongPass `{.02, .010}`, Trap/ShortPass `{.01, .005}`) and falls back to the
  legacy path whenever the proposal is rejected, so no action is ever dropped.
- No RNG is drawn by the takeover, no second `Humanoid::Process` runs, and no
  action/rule/player identity reaches `Ball`.

Verification (seed 42 fixtures, both processing orders, 4000 steps): the ON and
OFF simulations stay identical in RNG, ball position (< 1e-6 m), ball velocity
(< 1e-4 m/s), every player position/velocity/animation id/frame, and the
recorded accepted touches (`player`, `type`, `action_type`) right up to the first
migrated strike; at that strike the derived spin differs (the point impulse
replaces the legacy direct spin). OFF remains byte-identical to the previous
`--print-baseline` fingerprint.

**Default is OFF.** The technique table is uncalibrated engineering values (the
P5c-2 flight measurement shows an uncalibrated centre strike can move a 2 s pass
by ~22 m), and per the migration discipline the accepted action types only become
the default path after a documented baseline transition. Flipping the switch is
one call; it must be accompanied by saved old baselines, an explained diff and
re-calibrated technique values.

### P4d-2 prerequisite: contact authority

`DecideContactAuthority` fixes the active/passive precedence the passive switch
needs: a real passive body impact (nonzero normal impulse) owns the contact,
whether it is the same owner+part (no double resolution) or another player (they
arrived first); a zero-impulse overlap projection is not an impact and never
suppresses a valid active strike; otherwise the active strike owns it. Tests
cover all four input combinations.

## P4d-2: passive body production switch (implemented, opt-in)

`Simulation::EnableBodyPhysicsProduction(true)` routes the predicted body
colliders through the one authoritative `Ball::Step(BallTickInput)` and skips the
legacy `ResolveBallPlayerContacts` block entirely, so exactly one authority can
change the ball and no passive impulse is applied twice. The colliders fed to
production are the same upright list the P4d-1 `unified` comparison used
(`body_physics_production_colliders_`), which is now kept separate from the
low-pose proposal buffer so a sliding/trip proposal can never leak into
production.

Verification (seed 42, both processing orders, 4000 steps): on every tick with
shadow evidence the production ball state is **exactly equal** to the unified
kernel result (position, velocity, angular velocity), and the native tape
actually contains passive body contacts. The switch is OFF by default, so the
default fingerprint is unchanged; the legacy resolver still owns the contact when
the switch is off and is only skipped when it is on.

Remaining P4d-2 acceptance (not yet met, so the switch stays OFF): the low-pose
Sliding/Trip volumes are still uncalibrated, and fixed-scenario plus real
animation-phase ghost-blocking tests must be extended before the passive
authority becomes the default.

## P6: touch evidence classification (implemented, read-only)

`touch_evidence_classification.hpp` fixes the P6 vocabulary as a pure value
function so geometric contact, physical impact and the rule-facing AcceptedTouch
are never collapsed into one boolean:

- `GeometricOverlapOnly` - shapes overlap / sweep hit, no impulse, no rule fact;
- `PhysicalImpactOnly` - nonzero normal impulse without an attributed rule fact;
- `AcceptedTouchOnly` - rule fact without a recorded physical cause;
- `PhysicalImpactAccepted` - the nonzero impulse that is the rule touch.

`NeedsAuthoritativeRecord` marks the cases a Snapshot alone cannot disambiguate
(any accepted touch, and any unexplained physical impact), which is the P6 rule
for keeping a minimal authority record. Tests cover all combinations and prove a
zero-impulse overlap can never be promoted to a rule touch.

P6's rule-facing work (offside/corner/goal-kick/throw-in/last-toucher/shot/save
ordering against the AcceptedTouch history) and P7 (removing the legacy Ball
compatibility API) are **not done**: the AcceptedTouchSink is still the sole
rule-facing authority and the legacy `Touch`/`SetRotation`/`ApplyForce`/
`BallSpatialInfo`/duration-`Step` callers still exist because the P5c-3 and
P4d-2 switches are off by default. Removing them requires flipping those defaults,
which requires the technique and low-pose calibration plus an accepted baseline
transition.

## P5d: real per-tick arbitration wired into Simulation (implemented)

The active-touch observer now builds `ActiveImpulseCandidate` values from the
same captured inputs the P5c-2 model uses (physical impulse, closing speed from
`|J|/mass`, same-part passive fact), and Simulation calls
`ArbitratePendingActiveTouches()` exactly once per tick, in the common frame
right after both rosters. It runs the real `ArbitrateActiveImpulse` and
`DecideContactAuthority` with the tick's own unified passive evidence, so the
P5b/P4d-2 functions are exercised on production candidates rather than only in
unit tests.

Native seed 42, 4000 steps: normal order 11 candidate ticks -> 8 active wins,
3 passive wins (2 same owner+part, 1 other player); reverse 12 -> 12 active wins.
The same-owner/part and other-player suppressions are therefore applied to real
data.

What is **not** yet done for P5d: the Humanoid prepare/commit split, so the
winner is still not submitted through `BallTickInput::active_impulse`; the
opt-in P5c-3 path still uses `ApplyContactImpulse` at the touch instant. The
reorder (preview passive endpoint -> player preparation -> single
`Ball::Step(active_impulse)` -> commit player/rule state) is the remaining
piece, and it changes the capture/notification ordering, so it needs its own
baseline transition.

## P6a: minimal rule-touch record from the unified contact (implemented, measurement)

`CompleteBodyPhysicsShadow` now maps each unified contact's collider through the
Simulation-owned `ColliderId -> PlayerId/BodyPart` table and classifies it with
`ClassifyTouchEvidence`, producing a `RuleTouchEvidenceReport`:

- `contact_ticks == body_contacts + unmapped_contacts` - every contact is either
  an owned body contact or a pitch collider (ground/posts), which can never be a
  player rule touch;
- `geometric_only + physical_only + physical_accepted == body_contacts` - the
  three evidence kinds partition the mapped set;
- `accepted_without_impact` - an accidental accepted touch with no mapped
  physical cause.

Native seed 42, 4000 steps, normal/reverse: contact ticks 216/112, body contacts
215/112, unmapped 1/0, geometric-only 63/86, physical-only 152/26,
**physical-accepted 0/0**, accepted-without-impact 1/1.

That last line is the P6a gap stated numerically: the new CCD never yet produces
a rule-accepted touch, and one accepted accidental touch per order still has no
mapped physical cause. So the legacy `ResolveBallPlayerContacts` notification
remains the rule authority, and P6a's remaining work is to decide, per contact,
whether the physical impact *is* the rule touch (identity, time, last-toucher,
offside, restart consequences) instead of assuming `normal_impulse > 0`.

### Switch-combination regression (implemented)

`all four production switch combinations stay consistent and deterministic`
runs OFF/OFF, ON/OFF, OFF/ON and ON/ON in both processing orders for 1500 steps
each, twice per combination. For every combination: identical replay reproduces
RNG and the complete ball state, and the final ball state is finite/stable. For
every combination with the passive authority on, the production ball equals the
unified kernel result on every tick with evidence, including ON/ON - so enabling
the active takeover later in the same tick does not disturb the passive state
already committed by the single Step.

## Current P5e execution and remaining acceptance gates

The older P5d/P5e-1 measurements above describe post-execution observations, not
winner-authorized physical execution. They must not be read as completion of
the prepare/contact/event split. The previous immediate `ApplyContactImpulse`
API and `ApplyBallImpulse` adapter have now been removed.

`EnableActiveImpulseProduction` now selects `EnablePreparedTickProduction`.
The default is still OFF. `simulation_prepared_tick.cpp` performs:

1. const same-kernel passive preview (does not consume force);
2. one Player/Humanoid preparation pass against the passive endpoint and the
   previous authoritative history, with a const Ball reference and no legacy
   Ball mutation port;
3. global arbitration, with any genuine passive impact taking precedence;
4. the Tick's single authoritative `Ball::Step`, carrying at most one active
   impulse or an exclusive retain constraint;
5. publish only the winning proposal's exact provenance, then Player commit
   (fatigue/send-off; no repeated animation or RNG draws).

This intentionally replaces the legacy synchronous visibility among actors:
later actors do not read earlier uncommitted proposals. Mental images are
captured from the passive endpoint before preparation; newest ball predictions
are refreshed after commit. Referee/events receive committed physical state.
The Ball response contains the actually applied impulse/constraint. Shadow's
production endpoint records the actual committed state, not a substituted
passive-only state.

The winner and candidate buffer clear at every executed Tick entry, including
ceremonial/terminal exits, and on reset/end changes. Diagnostic passive evidence
is step/generation guarded and searches for the genuine impact after any zero-
impulse corrections. Diagnostic disable no longer leaves capture sticky; no
valid observations are silently dropped at a vector-capacity boundary.

`velocity_change_magnitude = |J| / mass` is the active ordering key, **not**
normal closing speed. Report `candidate_ticks` counts observed eligible
strikes, not distinct Ticks.

The production-only test now asserts that the current winner exactly equals
this Tick's applied Step impulse and that no winner persists on empty Ticks.
The four-switch test checks actual commit against passive response plus the
applied impulse/constraint. These are not full football-rule acceptance.

P6b publishes source-tagged RuleTouch after physical commit through the existing
AcceptedTouchSink. CCD touches require mapped active players, play authorization,
a genuine approaching impact and geometric-episode deduplication. Opponent
deflections preserve prior offside candidates; active strikes and retain
acquisition retain separate source provenance.

P4f only establishes Baked local-pose FK and anatomical geometry. These posed
capsules are not production inputs: the current CCD contract is translation-only
and cannot represent rotating/deforming adjacent poses. Prepared strikes still
use the zero-offset model, not a final calibrated foot/technique model.

Historical reach mismatches did not establish a height-cache cause. Historical
trajectory values compare `CompareActiveTouch(...).response`, a desired-center
surface projection, **not** the zero-offset `ProposeActiveTouch` response.
Neither those values nor a tautological velocity error constitute calibration.

`football_unified_acceptance [seconds|full] [reverse]` measures native ON/ON
terminal-match replay and Step latency. Full uses real 45-minute halves. Hashes
and impact counts are diagnostics, not reviewed new Golden or rule acceptance.
P7 default promotion/Golden replacement and removal of still-used legacy
Touch/SetRotation/duration adapters remain explicitly blocked by these gates.
