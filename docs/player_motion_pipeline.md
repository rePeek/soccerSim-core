# Player × Animation: reference motion → adapted root → final pose

Status: P0/P1 audit, P2 adapter, P3a root/pose warp and P4 locomotion
authority consolidation are implemented. P3b foot IK and P5–P7 are not.

## Target chain

```
PlayerControl / intent
  → Animation::Match        (standard clip + legal entry frame)
  → Reference motion        (local root XY/Yaw/Z + events + pose)
  → PlayerMotionAdapter     (body, capability, physical constraints)
  → Adapted root            (the only committed world motion)
  → Pose adapter            (stride/orientation warping, foot lock/IK)
  → Final PlayerFrame       (root + pose + contact geometry)
  → Collision / Ball / Runtime
```

Every tick has exactly one committed root/pose. Candidate previews are allowed;
only the final commit is authoritative.

## Authority rules

| Action class | Motion authority | Reference clip role |
|---|---|---|
| Running, walking, carrying | locomotion constraints | reference stride/cadence |
| Slide, jump, fall, dive | reference action + physical correction | primary trajectory |
| Pass/shot contact | reference action + contact events | contact timing/pose |
| Passive collision | physics reaction | reaction animation follows |

A generic 2-D acceleration/turn model must not replace slide/fall/header motion.
Conversely, a reference clip must not be a second world-position authority when
locomotion already owns the trajectory.

## Time and coordinate contract

- Stadium Frame: x along the pitch, y across, z up.
- Simulation tick: 10 ms (100 Hz).
- Clip frame time: continuous, measured in frames. One frame of clip time is
  10 ms at playback rate 1. The action clock (`PlayerActionState`) remains the
  only action-time authority; `clip_time` is derived from it and a playback rate.
- `MotionClip` stores local XY displacement only. Baked root Z is pelvis bob and
  is currently applied through the pose; it is **not** world height.
- Root yaw is not yet a separate baked track. Current orientation is distributed
  across `metadata.outgoing_angle`, `base_angle`, `rotationSmuggle`, `bodyFacing`
  and velocity-derived facing. Do not add a RootYaw track before the P0 audit
  proves which value is overall motion direction and which is torso-local.
- `PlayerKinematicState` is planar today. Adding world Z (jumping) requires P7
  and an audit of every consumer that assumes z == 0.

## P0 audit checklist

- [x] Reference XY sampling exists as `MotionClip` with fractional frame time.
- [x] `MotionClip` is read-only and never modifies `AnimationClip`.
- [ ] Root Yaw provenance audit (outgoing angle vs base angle vs smuggle).
- [ ] Root Z / pelvis bob separation in bake output.
- [ ] Foot-plant event derivation and calibration.
- [ ] Consumers of `GetFacing`, `GetDirectionVec`, `bodyFacing` and planar `z`
      audited before adding RootYaw/Z to public kinematics.
- [ ] Final-pose contact geometry and touch arbitration share one provenance.

## P2 adapter contract

`PlayerMotionAdapter::Step` is a pure numeric core:

```
reference_delta (world-aligned, one tick)
+ current PlayerKinematicState
+ PlayerBodyProfile
+ PlayerMotionProfile
+ desired velocity / authority mode
+ dt
→ constrained PlayerKinematicState + achieved_delta + constrained flag
```

Rules:

1. `achieved_delta` is the only world displacement result; callers must not add
   a second reference displacement.
2. Speed ≤ `max_speed`; acceleration/braking and turn rate use the existing
   `PlayerLocomotion` primitives.
3. Body height scales reference geometry, never ability limits.
4. Identity case (reference velocity == current velocity, no limit exceeded)
   must reproduce the reference displacement within a tight epsilon.
5. The adapter does not know about Animation, Ball, Runtime or collision. It
   returns the candidate root; collision/pose stages may later constrain it and
   must feed the final achieved delta into the pose adapter.

## P3/P4 status

- P3a (`PlayerPoseAdapter`) warps the final base yaw toward the achieved
  displacement direction and scales pelvis bob by the achieved/reference
  distance ratio. Identity inputs are bit-preserving.
- P3b foot planting/IK is **not** implemented: it requires P1 foot-plant
  events and a two-bone solve against the baked bind skeleton. The adapter
  therefore reports `foot_lock_applied == false`.
- P4: `PlayerMotion` is now a compatibility facade over
  `PlayerMotionAdapter` in Locomotion mode. The production locomotion path
  (`Player::CommitLocomotion`) therefore has one constraint core.
- P4 does **not** switch one-shot action root motion. Slide/pass/trip/fall
  still use the legacy `PlayerActionMotion` trajectory until P5/P6.

## Remaining phases

P3b foot IK, P5 pass/shot contact, P6 remaining actions, P7 vertical
root/jump/landing. Each has its own gate; they are deliberately not bundled
into this foundation.

## Validation gate (current)

P0–P4 are gated by the Player unit/link suite:

```sh
nix develop --command bash -c 'ctest --preset debug -R player'
nix develop --command bash -c 'ctest --preset release -R player'
```

A full 22-player real-tick match regression is currently out of scope for
these phases and is not a blocker for P0–P4.


## 2026 re-plan: source clips + procedural motion

The earlier P5–P7 labels are superseded by a cross-module sequence:

1. A0 mirror virtualization — **done** in `b38c653`.
2. A1 runtime procedural Autogen generation.
3. A2 283 authored source clips as the only physical animation assets.
4. P3a+ production Final Pose wiring.
5. P3b FootPlant + two-bone IK.
6. P5a/P5b Final Pose touch + TOI.
7. P6 action-by-action motion authority migration.
8. P7a–P7c world Z, airborne contact and landing.

### A0 result

- 726 authored GLBs remain; 726 mirror entries now carry `mirror_of` instead of
  a `file`.
- `animation_assets.py` derives mirror poses, roots, metadata, contact points and
  touches at bake time. The migration command is
  `animation_assets.py virtualize-mirrors [--write] [--delete-files]`.
- The 726 stored mirror GLBs were removed. A full bake was compared against the
  pre-A0 artifact: every clip is numerically equal; only signed-zero bytes differ.
- Player/animation/locomotion unit and link tests pass in debug and release.

A1 and later are **not** implemented in this commit.

## A1b/A2 result

- A1b: the offline `football_anim_baker --autogen-out` mode regenerates the
  443 non-mirror Autogen clips from the 10 legacy templates. CMake builds
  `autogen.simanim` before the Python catalog bake and passes it as
  `--autogen`.
- The generated Autogen poses differ from the removed GLBs by at most
  `7.45e-9`; roots are identical. Stored metadata is retained and validated
  within `2e-5`.
- A2: `assets/animations/` now contains 283 authored GLBs + manifest. 443
  entries are `procedural`, 726 are `mirror_of`.
- The generator still consumes `data/media/animations/templates/*.anim` and
  `player.object`; A1a canonical template GLBs are ready for the follow-up
  switch to a non-legacy input path.

## P3b result (heuristic support-foot lock)

- `PlayerFootIk` provides a deterministic two-bone solve with reach clamping and
  a support-anchor state machine.
- `PlayerBakedPoseSample` carries world-space foot targets; `Geometry()` and
  `ContactRegions()` consume the IK-adjusted pose.
- `PlayerExecution` updates anchors only for `e_FunctionType_Movement` clips;
  slide/trip/fall/header keep both feet free.
- Support selection currently uses the existing `SpatialState::foot`; this is a
  heuristic bridge, not the planned per-clip FootPlant event track.
- Tests cover IK reach, release, anchor persistence and final-pose consumption.

Remaining P3b gap: bake/sample explicit `FootContactPhase` events for all 283
sources + 443 procedural clips, then replace the `SpatialState::foot` heuristic.

## P7-lite result

- `PlayerKinematicState` now carries `vertical_offset` and
  `vertical_authoritative`.
- `PlayerBakedPoseSample` exposes the baked local root Z as the Player's world
  vertical offset.
- `EvaluateBakedBodyPose` consumes the authoritative world offset when set and
  falls back to the local baked bob otherwise.
- This is visual/geometry plumbing only: no gravity, vertical velocity, landing
  constraint, 3D body sweep or airborne ball arbitration.

## P3b/P6 updates

- `PlayerFootPlantState` now carries `FootContactPhase` (`Swing`, `Planting`,
  `Planted`, `Releasing`).
- P6: baked action root motion now commits through
  `PlayerMotionAdapter::KinematicReference`; the reference displacement is
  preserved and only the speed ceiling is applied. `PlayerActionMotion` remains
  the source of the reference trajectory, not a second commit authority.
- Touch preparation already consumes `BakedPoseSample`/`ContactRegions`, so P5a
  uses the IK-adjusted final pose. P5b (TOI + Runtime arbitration) is still
  outside the Player module and is not claimed complete.
