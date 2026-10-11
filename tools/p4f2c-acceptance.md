# P4f-2c body-collision acceptance

## Geometry vs committed pose

All 15 primitives (6 primary + 9 contact regions) are built from the same
committed realized skeleton (`BuildBakedBodyTape`), never from a collider-only
pose. `[baked-pipeline]` pins the six-body tape to `CommittedBakedPoses` without
Shadow authorization; `[baked-regions]` pins the nine extra IDs and mirror
geometry; `[pose-runtime]` rechecks every actual tape start/end against the
public realized skeleton in both processing orders.

## Identity

Stable ColliderId bands and the Simulation-owned owner map are covered by
`[baked-regions]` (ID bounds, primary separation, mirror), `[prepared-tick]`
(owner/part provenance through the referee) and `sim_production_lifecycle_test`
(15-ID owners rebuilt on Init, released on Stop). Left/right boots are distinct
IDs; the active-touch foot picker uses the boot nearest the ball, not the
unreliable `current_foot`/`touch_foot`.

## Sub-Tick interpolation (TOI/normal/response, not only distance)

The dense 1/16-Tick audit reports raw ~0.0113 m / realized ~0.0186 m endpoint
deviation. `[p4f2c][acceptance]` now perturbs a representative deforming kick
capsule by that measured realized bound and sweeps an approach grid: first
contact shifts < 0.4 ms (TOI), the normal rotates < 0.10 rad, and the normal
impulse changes by < 25% of its own magnitude. These are the stated geometric/
physical bounds; the linear transport model does not miss or fabricate a hit.

## Movement smoothing catch-up

`[pose-motion]` separates the raw Movement 1147 frame 7->8 ankle gait
(0.044-0.075 m, 4.4-7.5 m/s) from the realized runtime peak (~0.215 m/Tick),
which is entry-smoothing catch-up after a clip switch, bounded by the inherited
7.5*pi rad/s joint rate. The slerp entry bias was removed (P4f-2c-a); the rate
ramp remains; no collider-only velocity cap hides skeleton motion.

## Real-action scenarios

`[pose-runtime]` drives native Movement + injected Tackle (sliding) and TripMe
(fall) across 2400 ticks and both orders; `[prepared-tick]` exercises real CCD
touches and active strikes in a native AI run. Stop/turn/getup/header/save and
two-player contests rely on the same fixed-slot tape; they are exercised
transitively by the native ON/ON runs, not yet as separate pinned scenarios.

## Conclusion

P4f-2c acceptance passes the geometry, identity, interpolation-contact and
smoothing gates for Debug/core. Remaining scenario pinning (header/save/
two-player/early-foot as dedicated deterministic tapes) is deferred to G1-G3
coverage; no ghost-contact, missed-hit or stuck-ball evidence was found in the
native ON/ON replay.
