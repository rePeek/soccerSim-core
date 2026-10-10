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
