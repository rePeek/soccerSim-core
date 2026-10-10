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
