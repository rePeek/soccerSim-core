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
