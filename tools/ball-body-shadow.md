# P4 body collision migration evidence

## P4b-fix

The apparent missing `MentalImage(Match*)` ABI was an obsolete executable at
`build/release/test/football_sim_computation_test`, not the current target.
CMake's `football_configure_target` places executables at the build root. Use
CTest or `build/release/football_sim_computation_test`; do not use the stale path.
`nm -C -u` on that old file references Match; the current binary references the
Tick/span/Ball constructor. `ldd -r` of the current target resolves all symbols.
A `cmake --preset release --fresh` + `cmake --build --preset release --clean-first`
rebuilt the complete link chain. No compatibility constructor or library patch
was necessary. Geometry assertions use 1e-6 m tolerance for decimal float sums.

Endpoint error is sampled immediately after each actual Player::Process, before
pair collision offsets. Second-roster execution positions/velocities are rotated
back to the first-roster common contact frame using copies. No actor/RNG is
stepped twice. Resets discard incomplete prediction windows rather than treating
teleports as prediction failures. Stable IDs use the fixed home-then-away match
slot table (including inactive actors), never the active sweep order.

Reproduce: `nix develop --command build/release/football_body_shadow 4000`.
Seed 42, default teams, both processing orders, current P3c production physics:

| Order | Completed ticks | Endpoint samples | Mean m | Max m | <1cm | 1–5cm |
|---|---:|---:|---:|---:|---:|---:|
| normal | 3801 | 83622 | .000570067 | .03835535 | 83549 | 73 |
| reverse | 3800 | 83600 | .000582755 | .03836982 | 83428 | 172 |

| Order | Movement mean/max m | Trip mean/max m | Other animation mean/max m | Turning mean/max m |
|---|---|---|---|---|
| normal | .000518664/.00198747 | .00776203/.03835535 | .00299810/.02890410 | .000860947/.00717033 |
| reverse | .000515908/.00198634 | .00807870/.03836982 | .00279075/.01523561 | .000907345/.02330246 |

This native tape contains no sliding samples; it does NOT validate sliding.
Trip includes fallen root motion but three upright volumes still ignore pose.
Turning is velocity direction change >.05 radians in one Process at speed >.5m/s.

Dynamic-only FirstContact vs existing accidental AcceptedTouch (multiplicity
preserved, same completed tick and player; not a physical truth label): normal
215 queries hit, 1 accepted, 0 matched, 1 missed, 215 unmatched queries; reverse
112 hit, 1 accepted, 0 matched, 1 missed, 112 unmatched queries. Pending lower-body
contacts: 59/11. These include overlap/resting contacts and legacy cooldown/action
exclusions; they must NOT be turned into new rule touches as-is.

A/B tests disable only diagnostic shadow on one identical Simulation and replay
800 steps under both processing orders, including a physical end change. Ball,
actor positions/velocities/animation frames, possession, accepted facts and RNG
remain equal. No Golden or assets changed.

## P4c-1: explicit dynamic input

`BallTickInput { span<const ColliderMotion> dynamic_colliders; BallEnvironment
environment; }` and `BallStepResult Ball::Step(const BallTickInput&)` advance
exactly one 10ms tick. Static and dynamic bodies share AdvanceBallTick, followed
by the existing flexible net correction. IDs must be unique/nonzero/disjoint
from pitch IDs; invalid identity is rejected before state/force consumption.
A Ball-owned merge buffer pre-reserves 73 motions, grows for larger rosters
only as necessary, and retains capacity. Output contacts still use the existing
BallStepResult vector (a hit can allocate); no temporary input vector per tick.
Predictions and their legacy cache remain static-only and never borrow input.
The duration Step/force adapters remain until P7; Simulation still passes no
bodies. Independent Ball tests cover moving capsule, post/ground priority,
order/ties, result-state equality, invalid-ID atomicity, empty input equality
against static-only prediction and the duration adapter, plus net environments.
Release focused Ball/Simulation/regression tests passed, Goldens unchanged.

## P4c-2: response evidence and body material

BallContact now reports normal_impulse (N*s) and position_corrected.
SweepBall is only geometric evidence; AdvanceBallTick populates response fields.
ResolveContactResponse returns state/normal/tangential impulses, with the existing
ResolveContact wrapper retained. Surface arms use the current authoritative ball
center after projection; the returned contact center is also synchronized.
Tests pin tangential torque after deep overlap, zero spin for a pure normal
impact, stale center immunity, separating overlap with zero impulse, and no
next-tick separating surface hit. Sphere/capsule sweeps reject non-approaching
outside/tangent exit roots. This is a bug repair, not a cooldown mechanism.
Static production regression still passes; no Golden was rewritten.

All three body parts receive explicit Player-owned material { restitution=.35,
friction=.45 }, overridable at construction. Analytical tests pin velocity, spin,
Coulomb cap and energy loss. These values are provisional engineering policy,
not a claimed human-body realism calibration. Changing them only changes shadow
or external dynamic inputs until production authority is explicitly switched.
Release Ball/Simulation/regression tests pass.

## P4d-1: opt-in complete-world shadow (no authority switch)

`nix develop --command build/release/football_body_shadow 4000 --full` enables
the full comparison. Default runs keep only P4b geometry diagnostics. Internal
SimulationAccess enables one persistent diagnostic Ball, constructed with the
same BallConfig/Pitch as production. Each interval resets that diagnostic owner
to the same pre-legacy-contact BallState, runs static-only and static+dynamic
Ball::Step, and retains only the last completed interval and aggregate errors.
Both paths use the production kernel and netting correction, not a shadow-only
physics clone. Ball/shape/contact copies are rotated into the normal pitch Step
frame for reverse processing, then results return to the common first-roster
frame. Static ids keep their real pitch association. Actual production state is
sampled immediately after its Ball Step, before Player action impulses. Reset,
terminal, ceremony and exception paths discard incomplete evidence.

The reference production can have intervening legacy contacts/referee writes;
its delta therefore diagnoses composition differences, not pure kernel error.
Legacy accidental touches are compared by same completed tick/player with
multiplicity, never treated as the physical truth. `normal_impulse > 1e-6 N*s`
is the diagnostic impact gate; zero-impulse geometry is counted separately.
Per-contact samples report source id, owner/part, TOI fraction, projected ball
center, normal impulse, velocity and angular-velocity changes. Full Step results
remain available through the internal diagnostic accessor.

Seed 42, default teams, 4000 requested steps:

| Order | Completed | Static first | Body first | Nonzero body responses | Zero-impulse contacts | Matched/missed legacy accidental | Unmatched responses |
|---|---:|---:|---:|---:|---:|---:|---:|
| normal | 3801 | 1 | 215 | 152 | 63 | 0/1 | 152 |
| reverse | 3800 | 0 | 112 | 26 | 86 | 0/1 | 26 |

| Order | Unified−static position mean/max m | Velocity mean/max m/s | Spin mean/max rad/s |
|---|---|---|---|
| normal | .00875818/.311033 | .0952664/24.5854 | .322937/64.2508 |
| reverse | .00497535/.278621 | .0173524/5.93124 | .0765318/31.8869 |

Unified−production aggregates equal unified−static on this tape; this is not
an assertion that old body physics is generally equivalent. No dynamic query
was superseded by static geometry on this native sample. Constructed integration
tests explicitly exercise post-first and ground-first-over-later-body cases
under both orders, including physical pitch ids and owner mapping. A/B enables
the full shadow on one side and disables all shadow on the other: 800 ticks per
order plus end changes preserve Ball/actors/animation/possession/touches/RNG.

Important: every shadow interval starts again from the real production ball.
Repeated overlap/nonzero responses on consecutive intervals are NOT independent
real-world touches or a continuously integrated alternative match. No sliding
samples occurred; fallen-pose geometry and active-contact conflicts remain open.
These discrepancies block production switching and must not be hidden by a
cooldown, arbitrary tuning or Golden refresh. P4d authority and P5–P7 are pending.

The standalone `football_ball_allocation_test` counts zero allocations over 100
no-contact Step calls after warmup, for both 66 and 300 dynamic motions. It proves
input-buffer reuse including larger rosters, not zero allocations on impacts
(output contact vectors and prediction hits can allocate).

Final validation: complete Release CTest 42/42 passed (including full 45-minute
halves CLI, 370.91s). Focused Debug Ball/allocation/Simulation CTest 3/3 passed.
Core-only BUILD_TESTING=OFF / FOOTBALL_BUILD_APP=OFF configured and built.
No assets or regression Goldens changed. Every migration stage is a separate
local commit; nothing pushed.

## P4d-1.2: classify body impact episodes and validate action poses

Scope: diagnostics/tests only. Production BallTickInput, Ball::Step, BallState,
legacy body solver, AcceptedTouchSink, Referee, RNG and Snapshot remain unchanged.
No active impulse or production validity filter is introduced. Rerun with the
same `football_body_shadow 4000 --full` command. The original upright kernel
branch remains the reference, so 152/26 nonzero responses are still reproducible.

### Definitions and attribution limits

- GeometricContact: every body whose initial sphere-surface gap <= 1e-6m OR
  whose straight initial-velocity SweepBall finds a contact in this interval.
  The actual complete-kernel winner is also retained even if only gravity/drag
  made its probe hit. Includes resting tangency, even when CCD correctly rejects
  a separating exit root. Non-winning parts are counted, but these raw queries
  are not an exhaustive enumeration of the accelerated probe's contacts.
  Candidate counts mean within-tick contenders, not an assertion of simultaneous
  TOI or multiple solved impacts.
- PhysicalImpact: the earliest complete-world kernel body response has normal
  impulse > 1e-6 N*s. Initial penetration and position_corrected are independent
  diagnostics. An overlap projection is not itself a physical impact.
- AcceptedTouch: only existing rule evidence from the real production path.
  Accidental matches preserve multiplicity. Same-player intentional touches in
  the interval are separately counted; these include legacy BallControl/retain
  touches and do not prove an actual Shot/Pass release at the predicted window.

Fixed collider slots track consecutive observed episodes. Same collider, next
executed step, same generation, and no known separation at the new boundary
continue an episode. Known positive initial gap starts a new one; absent shapes,
step gaps, resets, disabling diagnostics and end changes cannot bridge it.
Geometric episodes and consecutive nonzero-impact runs are separate counters.
These observational runs still reset from production each tick: neither an
episode nor a nonzero run is a certified count of independent real touches.
State is bounded by match collider slots and lives only in Simulation diagnostics.

Touch window is [elapsed, elapsed+1] crossing the scheduled action contact tick.
Pending and past frames are distinguished; IsContactDue() is NOT interpreted as
an indefinitely open window. Foot conflict means the winning lower-body collider
plus Shot/Pass/Trap/BallControl in that boundary window; it is a warning, not an
accepted active candidate. Header is not a lower-body foot action.

Legacy flags are frozen BEFORE the live sweep: inclusive <=15 cooldown, own
recent touch, no recent opponent touch, excluded action, unique possession,
Interfere/Deflect unexpected-direction gate, unavailable history, play/period
gate, old shrunken-radius cached-position geometry miss and controlled routing.
Flags overlap and do not sum to a partition. The old solver mutates touch biases
within its roster sweep, so these facts are not a counterfactual rerun verdict.
No cooldown or possession flag suppresses a Ball response in this change.

### Same native input generator, different native trajectories

Seed 42, default teams, both orders, 4000 requested steps:

| Order | All geometric candidate samples | Geometric episode starts / continued | Longest run ticks | Nonzero impact runs / continued | Initial penetration impacts | Started separated impacts |
|---|---:|---:|---:|---:|---:|---:|
| normal | 348 | 11 / 337 | 137 | 15 / 137 | 143 | 9 |
| reverse | 208 | 5 / 203 | 92 | 3 / 23 | 25 | 1 |

All penetration impacts in this tape also have position_corrected=true.
The 348/208 samples include non-winning parts and therefore differ from the
215/112 winning body contacts. Counts do NOT turn 152 responses into 15 touches.

| Action | Normal player-ticks | Normal impacts (new geometric run / continued) | Reverse player-ticks | Reverse impacts (new geometric run / continued) |
|---|---:|---:|---:|---:|
| Movement | 82313 | 64 (1 / 63) | 82215 | 0 |
| BallControl | 126 | 2 (1 / 1) | 37 | 0 |
| ShortPass | 875 | 40 (5 / 35) | 703 | 12 (2 / 10) |
| Shot | 36 | 22 (2 / 20) | 100 | 0 |
| Deflect | 51 | 24 (1 / 23) | 84 | 14 (0 / 14) |
| Trip | 221 | 0 | 461 | 0 |

No sliding action occurs on this native tape. Fixed tests provide sliding
coverage instead; native sliding realism is still unmeasured.

Overlapping pre-sweep flags on the 152/26 nonzero responses:

| Flag / observation | Normal | Reverse |
|---|---:|---:|
| Old global cooldown | 0 | 0 |
| Recent own touch | 97 | 19 |
| No recent opponent touch | 141 | 26 |
| Unique possession | 105 | 21 |
| Action excluded by old solver | 64 | 12 |
| Unexpected-direction gate | 13 | 14 |
| Old cached/shrunken geometry miss | 33 | 7 |
| Controlled routing | 44 | 7 |
| Multiple parts of winning player contend | 96 | 15 |
| Multiple players contend | 0 | 0 |
| Scheduled boundary window | 5 | 0 |
| Pending scheduled contact | 60 | 7 |
| Lower-body boundary foot conflict | 3 | 0 |
| Same-player real intentional touch in interval | 97 | 14 |

For example, normal Movement's 64 impacts mostly concern a nearly stationary
ball: mean ball/player/CCD closing speed .3525/.2360/.1727 m/s. Its 57 unique-
possession impacts also have recent-own-touch flags and actual intentional
touches. Normal Deflect's 24 impacts have a stationary ball and all have unique
possession/recent-own-touch; reverse Deflect's 14 have the same flags but much
smaller mean closing speed (1.9539 vs .02621m/s). ShortPass/Shot scheduling
occupancy and physical overlap differ between orders. This provides input-state
and continuing-overlap explanations for a large portion of the count gap, NOT
proof that all frame bugs are impossible. Frozen identical collider tapes below
separate kernel order/rotation correctness from native input divergence.

### Provisional low-action pose branch

`player_body_pose_shadow.hpp` builds horizontal torso/leg capsules and a low
head sphere for Sliding/Trip. All other actions keep the original upright
geometry. Sliding legs extend along the explicitly supplied common-frame axis;
Trip lies along that axis with legs behind. Torso z=.24/r=.20m, legs z=.16/r=.13m,
head z=.25/r=.12m. These are labelled engineering proposals, not fitted skeleton
poses. The pose/axis is frozen over one 10ms translational sweep. Simulation
converts a copy of roster-local bodyFacing explicitly because legacy kinematic
Mirror intentionally does not rotate it. No Humanoid is stepped to obtain a pose.

A separate posed kernel runs only on intervals with Sliding/Trip. On this tape,
normal has 151 such intervals, 64 changed first-collider identities, 38 added
nonzero responses, zero removed, total proposed body responses 190; reverse has
353 intervals, no changed first identities, total 26. A low posture can ADD low
contacts as well as remove upright ghost blockers; matching old counts is not
an objective. This tag-level proposal does not describe early fall, recovery,
rolling or skeleton orientation. Pose transition validity/phase calibration is
still required before it can become authoritative.

### Fixed scenarios and continuous trajectories

`test/player_body_contact_diagnostics_test.cpp` covers:

- standing lower-body impact and running body approaching an incoming ball;
- sliding forward-leg passage outside the upright volume, with a real impulse;
- fallen player's former chest space letting a high ball pass, while low torso
  space still collides (the critical upright-ghost regression);
- fixed Shot/all Pass/Trap schedules with a reachable contact point and passive
  lower-body overlap at the boundary; pending/boundary/past classification;
- two contestants with tied lower-body TOIs, multi-part overlap, stable id winner
  and a single response regardless of input ordering;
- three 120-tick continuously integrated stationary-body tapes (standing/sliding/
  fallen), with exactly one body impact, subsequent separation, finite spin and
  lower final speed. Permuted and 180-degree rotated inputs replay equal position,
  velocity, spin, contact source and TOI (tolerances 2e-5m and 1e-5 fraction);
- separating deep overlap projection with zero impulse and no subsequent body
  hit over 20 ticks; explicit mirrored pose-axis conversion and episode breaks.

Integration tests retain full-shadow-vs-disabled 800-step A/B under both orders
and end changes, exact Snapshot/actor/animation/possession/touch/RNG equality,
owner/part mapping, and old cooldown's exact 15/16 boundary without suppressing
shadow physics. Gravity-only head contact tests ensure every kernel impact
remains classifiable even when raw straight-motion queries miss. The action
scenarios use fixed action schedules, not real baked foot trajectories or an
active-impulse implementation; those belong to P5a.

**P4e resolved the moving-body overlap freeze (production kernel).**

`AdvanceBallTick` now separates position correction from impact. A contact whose
TOI is zero while the relative normal velocity is separating or resting is an
instantaneous projection: it is reported with `normal_impulse == 0` and
`position_corrected == true` and does **not** consume the interval. The ball
keeps its free motion (gravity/drag/grass recomputed from the corrected cursor)
and may still find one genuine impact later in the same tick. Only an
impulse-bearing contact ends the tick, so there is at most one new impact per
tick while corrections may repeat up to `colliders + 2` for termination.

The continuous tape (lower capsule at -1m/s, incoming ball +20m/s) previously
froze: after the real impact the ball was projected to the START shape every
tick with zero impulse and never advanced. Now the real impact on tick 0 is the
only impulse; following ticks produce no new impulse, the ball keeps its
outgoing velocity and clears the moving END shape by more than 5cm within one
tick, advancing more than 5cm per tick. The rewritten test asserts this plus a
ceiling of one contact per tick and no invented follow-up. The
`deep separating overlap projects once but does not invent a followup impact`
case still passes.

Production note: production still supplies no dynamic bodies, so the pitch
collider path is unchanged. Regression `--print-baseline` is byte-identical and
the native `football_body_shadow 4000 --full` shadow metrics are unchanged
(215/112 winning contacts, 152/26 effective impacts, 63/86 zero impulse).

### Validation and next gate

Release CTest 42/42 passed, including full regulation CLI (361.40s). Debug
CTest 41/41 passed without full regulation CLI (598.96s). Regression and both
animation A/B regression modes passed in both builds. Core-only build passed.
Focused diagnostics tests passed 190068 assertions in 13 cases. No Golden/assets
changed, no push.

P5a may proceed ONLY as shadow candidate/evidence work: capture the existing
action's target velocity and physical contact point once at the real accepted
action frame; form J = mass*(target_velocity - passive_endpoint.velocity), then
compare linear/angular response and old success/rejection reasons without RNG
redraws or production writes. Simulation must distinguish a reachable accepted
active candidate from merely a pending animation. The conflict observations
above argue against suppressing all lower-body physics throughout an action.

Before P4d-2/P5b, explicitly decide same-owner/part endpoint precedence, other-
player passive priority, at-most-one active candidate arbitration, and separate
position correction from a new impact. Keep Ball unaware of action/rule identity.
Moving-overlap release and phase-valid low poses remain blocking; P5a is not
permission to open the production switch.

## P5a: read-only active-touch candidates (no authority switch)

`player/player_active_touch_shadow.hpp` observes the *existing* action execution
path; it never recomputes an action, draws RNG or writes production state.
`football/ball/ball_impulse.hpp` is a pure physical value
`BallImpulse{impulse, contact_point}` with no player/action/rule identity.

At each real scheduled/controlled contact the Humanoid records, before and after
`ApplyBallTouch`, the executed action, animation id, contact frame, elapsed
frames, ball state, the baked desired ball center (`currentAnim.touchPos +
positionOffset`) and the player position. Pending frames and authorization /
distance / height rejections are recorded separately, so a pending animation is
never reported as an accepted touch. `Simulation::ActiveTouchEvents` mirrors the
second processing team into the pitch frame (state, target velocity and points)
before `ActiveTouchShadowReport::Record`, exactly like the P4 body shadow.

`CompareActiveTouch` starts from the predicted passive endpoint
(`body_physics_shadow_tick_.unified.state` for the current step), derives the
contact point by projecting the raw desired ball center onto the current ball
surface and forms `J = mass * (target_velocity - passive_endpoint.velocity)`,
then applies `ApplyImpulseAtPoint`. The report separates:

- `passive_endpoints`: comparisons that had a same-step passive endpoint;
- `same_part_conflicts` / `other_player_conflicts`: the endpoint's winning body
  impact belongs to the same player+part as the candidate / to another player;
- `fallback_points`: the raw desired center coincided with the ball center, so a
  provisional `player_position + (0,0,.1)` proxy was used. This is reported, not
  treated as proof of a posed foot contact.

Native seed 42, normal/reverse (`football_body_shadow 4000 --full`): 604/457
pending samples, 3/3 controlled, 1/1 retain acquire, 131/90 retain anchors,
1/1 retain release. Executed actions: movement 2/3, ball_control 1/1, short_pass
10/9, shot 0/2, trip 1/0. No duplicate candidate, no contending tick, no dropped
detail. `same_part` was 2/0 (short pass) and `other_player` 1/0, i.e. a real
same-part passive/active overlap exists on the native tape but is not yet
arbitrated.

**Measured semantic gap, not a candidate failure:** because the target velocity
is the legacy post-touch velocity, the derived linear impulse reproduces it
(`dv_max = 0`). The point impulse cannot reproduce the legacy *directly set*
spin: `dw_max` reached 446/348 rad/s. Legacy `SetRotation` assigns angular
velocity independently of the contact, so a single physical point impulse is
inconsistent with it. P5b must define the outgoing spin as a physical
consequence of the impulse, not as a copied legacy value.

P5a is evidence only. `BallTickInput` still has no `active_impulse`, production
passes no dynamic bodies and the acceptance blockers below remain open.

Validation: focused `[active-shadow]` cases (2 cases, 380343 assertions) prove
the endpoint impulse changes only velocity/spin about the contact point (position
and orientation preserved), rejects non-executed/retained stages, is
mirror-consistent, and that 1600 native steps under both processing orders with
end-change replay keep RNG, ball state, player state, accepted touches and
in-play facts identical to a shadow-free control. Regression `--print-baseline`
is byte-identical and the baked asset hash is unchanged.

## P5b: endpoint-impulse contract and deterministic arbitration (switch closed)

`BallTickInput` now carries an optional `BallImpulse active_impulse` and
`BallStepResult` echoes the impulse actually applied. It is applied inside
`Ball::Step` **after** passive motion and the netting correction, changes only
velocity and spin about the supplied world point, never advances position again
and never runs a second Step. This is the tick-endpoint timing the legacy
`ApplyBallTouch + SetRotation` already had; the ordering is not fixed by
reordering phases, because the impulse is a state change at the same point in the
tick where the legacy touch happened, so it becomes the next tick's initial
state exactly as before.

`player/player_active_impulse.hpp` owns the stateless arbitration contract.
Candidates are `{player, body_part, BallImpulse, closing_speed, passive_same_part}`.
Candidates whose owner+part already produced this tick's passive impact are
dropped (no double resolution); the remainder is ordered by larger closing speed,
then lower `PlayerId`, then lower body-part enum. At most one candidate wins, and
an empty result means no active impulse. The function is pure, so permuting the
input cannot change the winner and mirror symmetry is preserved by the callers'
existing frame conversion.

Tests: `ball_tick_input_test.cpp` proves an active impulse leaves position and
orientation equal to the passive endpoint, changes linear velocity by exactly
`J/mass`, adds offset spin, replays as ordinary state on the next plain Step, and
mirrors exactly. `player_active_touch_shadow_test.cpp` proves unique selection,
order independence, tie-breakers, and passive same-part suppression.

**Production switch deliberately stays closed.** Nothing constructs an
`active_impulse` in production: `Simulation` still lets the Humanoid drive
`ApplyBallTouch`/`SetRotation`, so the endpoint-impulse path is exercised only by
direct tests. It will not be enabled until the body-pose calibration, same-part
active/passive precedence and moving-body acceptance blockers are resolved, and
the P4d-2 passive-body switch is decided. Regression `--print-baseline` remains
byte-identical and Release CTest is 42/42.

## Consolidated validation and remaining blockers (after P5b)

Stages delivered in this run, each a separate local commit on `main`
(author `rePeek <senxlin@gmail.com>`, nothing pushed):

- P5a `dec2fd2` read-only active-touch candidates;
- P4e `27588a3` overlap-only projection no longer consumes the tick;
- P5b `869c59a` endpoint active impulse + deterministic arbitration (switch closed).

Release CTest 42/42 including the full regulation CLI (360.21s). Debug build
succeeded; focused Debug suites passed 570270 assertions in 11 cases plus 7354 in
49 ball cases. Regression `--print-baseline` is byte-identical in Release and
Debug; both animation A/B modes pass; the core-only build passes. The baked asset
SHA256 is unchanged
(`33ab837652da93a795e4886738ca09b92e4b6376c99a2500202572e0a7885b86`) and no
Golden was refreshed. Native `football_body_shadow 4000 --full` metrics are
unchanged from P4d-1.2 (215/112 winning contacts, 152/26 effective impacts,
63/86 zero impulse), so P4e did not alter the existing tape.

Still blocking a production authority switch, in order:

1. **P4d-2 (passive body switch)** is not started. It needs same-owner/part and
   other-player passive priority decided against the P5a conflict evidence, plus
   the endpoint-impulse contract above.
2. **Body pose calibration** is not done. Low Sliding/Trip volumes are a
   separately labelled proposal; they add contacts rather than merely removing
   upright ghosts, and no real skeleton/foot trajectory validates them.
3. **P6/P7** (remove the legacy duration/force adapter and the legacy touch path)
   are not started.
4. Native seed-42 tape still has no sliding samples, so passive-body behaviour
   under real sliding remains unmeasured.

The production endpoint-impulse path is exercised only by direct tests; the
Simulation still drives the legacy `ApplyBallTouch`/`SetRotation` path.
