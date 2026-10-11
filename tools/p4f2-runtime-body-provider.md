# P4f-2 runtime body provider — experimental checkpoint

## Contract

`Player::GetBakedPoseSample` copies the actual selected clip/frame, Player state,
base yaw, action, scale and explicit end-clamp status. It is read-only; it does
not advance Humanoid, execute an action or draw RNG. This is a raw animation
**target**, not proof of a continuous rendered pose. Root XY belongs to Player;
only baked player-node Z is added. Non-root translations remain ignored.

Raw targets jump at clip switches. `RealizeBakedPose` declares a shared skeletal
realization policy, adapted from the simple smoothing branch of offline
`Animation::Apply`: entry blending, local rotation limits and entry root-Z
blending. Fixed smoothing factor one is an explicit new policy, not a claim of
bit-identical legacy render smoothing. Both public committed skeletal data and
physical primitives use that realized pose; no collider-only clamp, rejection,
cooldown or collision exclusion hides a target jump.

`Simulation::EnableBakedBodyProduction` requires prepared+body production, and
remains internal/default OFF. `simulation_baked_body.cpp` owns the realized
history and a dedicated six-segment tape. Diagnostic coarse/low-pose buffers
cannot overwrite that tape. Slots match SnapshotMetadata. Stored poses are in
the first-roster common frame; second-roster raw samples are transformed into
that frame. Collider geometry is transformed separately into pitch physics
coordinates for Ball, without changing identity.

At Tick entry, root pair corrections from the previous Tick are already Player
placements and are not turned into inferred surface speed. Reset/end-change
history is reseeded rather than swept across a placement. Inactive actors have
no inputs. Disabling a prerequisite or Stop invalidates the provider.

Actors share one const forecast endpoint, using the known next clip sample and
root velocity; this is not another actor pass. After the single real preparation
pass, actual pose endpoints replace the forecast tape. A const same-kernel
preview refreshes arbitration/passive evidence before the **one authoritative
Step**. Actors are not rerun and proposals are not delayed. Forecast error is
not yet technique/reach calibration acceptance.

`CommittedBakedPoses()` exposes the very same skeleton at physical commitment.
It is not a post-rule placement snapshot; placement invalidates the view until
realization resumes. Inter-Tick primitive transport is explicitly endpoint
linear, not rigid joint SLERP. Consumers cannot claim a different sub-Tick
render interpolation is identical: its deviation still needs measured bounds.
Material transport remains P4f-1 minimal twist, not inferred skin axial spin.

Four physical leg IDs remain distinct; rule episodes aggregate those surfaces
by Player/lower-body part. A leaf separating cannot rearm another overlapping
leaf. Physical provenance keeps the original anatomical ColliderId.

## Named mesh envelope calibration

Tests read the immutable local ASE vertex sets offline; runtime never loads
meshes. Head: 222 vertices, center `(0.0003,-0.0121,0.1212)` relative to neck,
maximum distance 0.171231 m, chosen sphere radius 0.172 m. Thigh: 207 vertices,
maximum distance to local hip/knee segment 0.105971 m, radius 0.106 m. Shin: 224
vertices, maximum distance to local knee/ankle segment 0.101152 m, radius 0.102 m.
Tests transform every named mesh vertex through representative reference,
Sliding and Trip phases at two scales and require enclosure. Mesh paths are
fixed test-only CMake inputs, independent of CTest working directory.

This does **not** assert whole-avatar enclosure: torso remains provisional;
arms, hands, boots and cosmetic hair are not covered by these three proofs.
The initial runtime reference clip is tested explicitly rather than inventing a
Movement metadata clip: the shipped library has no Movement/None-tagged clips.
A running-gait/render-source contract remains a further calibration question.

## Evidence and remaining gates

2400-Tick raw target/realization tapes in both processing orders, with actual
Trip and Sliding controls, preserve observed vs unobserved state, orientation,
animation and RNG exactly. Excluding generation changes, raw maximum switch
joint displacement was 0.993583 / 0.701337 m; realized switch displacement was
0.120441 / 0.137026 m. Maximum realized displacement across all transitions was
0.214825 / 0.214822 m. These are geometry displacements, not accepted physical
speed maxima or a claim of biological realism.

The provider replay checks every actual tape start/end against the public
realized skeleton in both orders and compares active Shadow ON vs OFF execution.
A constructed live head-strike placement establishes real six-ID impulse and
RuleTouch owner provenance. The non-injected 24-second replay produced zero
body impacts; do not label constructed impacts as spontaneous native coverage.

P4f-2 production acceptance is still incomplete: actual sub-Tick interpolation
error, torso/boot/hand coverage, gait and broader transition/lifecycle scenarios
need acceptance. P4f-3 actual strike point, technique/precision/power/foot/spin/
flight calibration; P6c full semantic rules and full-regulation final-source
replays; and P7 reviewed Golden/default promotion/Legacy deletion are pending.
Assets and Golden remain unchanged. No completion/promotion claim follows from
passing helper or deterministic replay tests.

## Checkpoint validation

Final source: Release CTest 42/42 (401.69 s), Debug CTest 41/41 (921.31 s,
excluding full-regulation CLI), core-only build passed. Default-OFF regression
output is byte-identical to `/tmp/ball-7e71690-baseline.txt`. The first full test
attempt exposed a CTest working-directory dependency in the offline mesh test;
that path was corrected explicitly, then both full suites were rerun.

Logs: `/tmp/p4f2-final-release-ctest.log`,
`/tmp/p4f2-final-debug-ctest.log`, `/tmp/p4f2-core-build.log`,
`/tmp/p4f2-baseline.txt`. Test duration is not Tick latency. These default suite
runs are not full ON/ON Baked-source regulation/rule/performance acceptance.
