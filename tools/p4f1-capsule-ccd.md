# P4f-1 — deforming capsule CCD and stable anatomical identities

## Boundary

BallState, the unified Tick kernel and P5e's Prepare → Arbitration → Step → Commit boundary were accepted by the maintainer at `e4298fb`. This stage does not split Humanoid again, add a Ball member API, import meshes, alter animation assets, or switch production defaults. It extends the existing ColliderMotion and BallContact physics-data contracts.

## Motion contract

For normalized Tick time `t ∈ [0,1]`:

- `a(t)=a0+(a1-a0)t`, `b(t)=b0+(b1-b0)t`;
- `r(t)=r0+(r1-r0)t` (nonnegative endpoints);
- the primitive at t is exactly the capsule on segment `[a(t),b(t)]` with radius r(t);
- spheres interpolate center and radius; planes remain static; primitive type stays fixed.

Unequal endpoint displacements change axis orientation and length. This is **endpoint-linear deformation**, not rigid quaternion/arc interpolation: bone length can shorten between widely rotated endpoint poses. A full revolution with identical endpoint poses cannot be inferred. P4f-2 must validate that adjacent animation samples are a suitable approximation or supply a finer motion representation; do not emulate arcs with extra Ball Steps or duplicate ColliderIds in the current input span.

The translated constant-radius fast path remains in place. New code is private to `football::ball` in `src/sim/ball/src/ball_deforming_capsule.{hpp,cpp}` and has no Player, animation, rule or RNG dependency.

## First TOI

A conservative swept AABB contains all interpolated endpoints/radii. For the narrow phase let `q(t)=ball(t)-a(t)`, `d(t)=b(t)-a(t)` and `R(t)=ball_radius+r(t)`. Interior contact roots satisfy:

```
|q(t) × d(t)|² - R(t)² |d(t)|² = 0
```

This is a quartic. The two end caps supply quadratics. Derivative roots partition [0,1] into monotone intervals; each sign-changing root is bracketed and bisected with 64 deterministic iterations. Candidates are checked against the **finite** capsule and incoming normal speed; separating/exit roots and false roots at a collapsed axis are rejected. Initial penetration is reported at time zero separately from impulse response.

There is no fixed temporal sampling grid, arbitrary motion-speed cutoff, contact cooldown or root-buffer drop. Polynomial work uses double precision and fixed stack arrays; public evidence remains float. As elsewhere in CCD, this is floating-point geometry, not exact arithmetic: candidate distance tolerance is 1e-6 m and the returned TOI is float. Exact stationary roots can be retained; zero-normal-speed tangency is not promoted to a genuine impact. Tests use a separate dense distance oracle as a check, not as the algorithm.

Very early contact correction now uses the interpolated pose at the actual TOI, not a rotating collider's START shape. A nonzero short capsule is not silently treated as a point during projection.

## Contact surface velocity

Let s be the closest axis parameter at TOI. The declared material transport is minimal-twist:

```
v_axis = (1-s) v_a + s v_b
omega  = d × d_dot / |d|²
v_surface = v_axis + omega × (r n) + r_dot n
```

For a collapsed axis omega is zero. Axial twist cannot be inferred from endpoints, so its component is zero. This defines the simplified capsule's transport; it does not claim reconstruction of all mesh/bone axial spin. The nearest-point parameter's time derivative is **not** a material velocity and is not added.

`BallContact::surface_velocity` records this velocity. `AdvanceBallTick` supplies it to the existing point-impulse/Coulomb-friction response rather than looking up endpoint A's velocity. Both Simulation frame adapters mirror this new evidence. Spin still comes from a point impulse, never angular-velocity assignment. The Tick still resolves at most one genuine passive impact, followed by the existing arbitrated endpoint input.

## Stable IDs and pose tapes

- Pitch IDs: 1–7.
- Coarse three-part band: `[0x80000000,0xc0000000)`; ordinary existing IDs are unchanged. Overflow and invalid part enums are rejected before spilling into the anatomical band.
- Baked six-segment band: `[0xc0000000,0xffffffff]`, encoded as `base + fixed_roster_slot*6 + segment` with 64-bit range checks.
- Segment order: torso, head, left thigh, left shin, right thigh, right shin. Four leg identities may all map to `PlayerBodyPart::LowerBody`, but their physical IDs are different.

`BuildBakedBodyColliderMotions(start_geometry,end_geometry,fixed_slot)` emits six ColliderMotions using caller-supplied FK pose endpoints. IDs are independent of geometry, traversal order, PlayerId and frame mirroring. Tests build these tapes for every adjacent frame of every real Baked Sliding/Trip clip. This verifies the builder's endpoints/IDs, **not** final actor-provider/action-switch continuity or production rule episode registration. Production still uses the existing upright tape until P4f-2/P4f-3/P6c acceptance.

## Tests

`src/sim/ball/tests/ball_deforming_capsule_test.cpp` links only Ball/model/foundation and covers:

- stationary ball hit mid-Tick despite clear start/end poses;
- thin fast sweep that a coarse sampling grid misses;
- stretch, radius dilation and variable-radius sphere;
- collapsed/zero-length axes, invalid inputs and unconsumed pending force on failed Step;
- initially receding motion with a later genuine entry;
- separating overlap correction with continued progress;
- local surface speed, distance derivative and friction/spin response versus the wrong endpoint velocity;
- very early nonzero TOI pose agreement;
- arbitrary spatial rotation/common translation, exact 180° mirror, replay and permutation/tie order;
- earlier static impact competition;
- 400 generated 3D cases against 4,001 independent distance samples each.

Allocator interposition verifies 1,000 hit and 1,000 finite-feature miss queries with **zero** allocations. Existing warmed Step tests still verify merge reuse for 66/300 colliders; no claim is made that contact-result vectors allocate nothing.

The pose suite covers six distinct IDs across 100 fixed slots, reversed/mirrored construction, part mappings, both reserved bands and overflow boundaries, plus actual adjacent Baked pose endpoints.

## Validation

Final-source Release Ball: 8,355 assertions / 60 cases; Debug CCD: 960 / 10. Allocator: 7 / 2. Pose: 265,076 / 4. Release CTest: 42/42 (412.60 s); Debug: 41/41 (791.02 s), excluding the full-regulation CLI. Core build passed. Default-OFF regression is byte-identical to `/tmp/ball-7e71690-baseline.txt`. Logs: `/tmp/p4f1-final2-{release,debug}-ctest.log`, `/tmp/p4f1-final2-{ball,alloc,debug-ccd,core-build}.log`. Golden/assets/defaults remain unchanged; no push.

## Next gates (not included in P4f-1)

P4f-2: actual actor pose-provider timing, standing/running/sliding/trip/recovery and clip-switch continuity, acceptable interpolation error, six-ID owner/episode registration.

P4f-3: baked foot/body strike position and calibrated technique/spin/Magnus/reach/flight evidence.

P6c: constructed rule consequences including deliberate play versus deflection and restart/GK/lifecycle ownership.

P7a/b/c: only after the above pass, production promotion, reviewed new Golden, then behavior-neutral Legacy removal. AcceptedTouchSink stays as the referee submission port.
