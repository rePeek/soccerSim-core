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
