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
- Support selection derives from the *gait* ankle heights (lower foot, with a
  1 cm switch margin) instead of `SpatialState::foot`, which drifts out of
  phase once the playback rate is motion-driven.
- Tests cover IK reach, release, anchor persistence and final-pose consumption.

Remaining P3b gap: bake/sample explicit `FootContactPhase` events for all 283
sources + 443 procedural clips, then replace the height heuristic.

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

## M1 result: one motion authority per tick

- `PlayerMotionAdapter` stays the only numeric constraint core. For a
  `KinematicReference` tick its `achieved_delta` **is** the world displacement,
  and the committed velocity is `achieved_delta / dt`; the unconstrained
  reference velocity is no longer published as the Player's movement. When the
  speed ceiling binds, `spatialState.movement`, `floatVelocity`, `enumVelocity`
  and the body-direction quantization all consume the capped value.
- The action path commits through `PlayerExecutionPort::CommitActionMotionState`
  with the whole constrained `PlayerKinematicState`. `CommitLegacyActionMotion`
  now remains only for reset / factual-state adoption and can no longer become a
  second writer of a locomotion or action-motion tick: the non-locomotion
  `ProjectMovementState` branch refreshes the projection and asserts the
  kinematic oracle instead of re-reading the projection into a fresh state.
  This is what previously dropped the vertical (and any future) motion fields
  on a projection round trip.
- A position correction (`ApplyPositionCorrection`) still only changes position:
  an instantaneous displacement never becomes velocity, and never divides a
  penetration fix by `dt`.
- Gate: `player_unit` covers the capped/displacement/velocity consistency and
  the "locomotion ignores the animation root trajectory" rule (identical
  achieved motion with no, matching and wildly different `reference_delta`).

## M2 result: motion-driven locomotion playback

Order per ordinary locomotion tick:

```
CommitLocomotion                        (Player solves the motion)
  → LocomotionAnimationInput snapshot   (achieved velocity, desired, body)
  → Match / locomotion preference        (clip identity)
  → LocomotionPlaybackRequest            (rate, continuous clip time, phase)
  → PlaybackState::PlayAtTime            (fractional pose sample + blend)
  → FootPlant / final pose / contact      (unchanged consumers)
```

- `LocomotionClipReferenceSpeed` is the one definition of a clip's native speed,
  shared by selection (`locomotion_animation_strategy.hpp`) and playback, so the
  two cannot disagree.
- `AdvanceLocomotionPlayback` is pure and stateless apart from the carried
  request: `clip_time += clamp(achieved/reference, 0.35, 2.2)` frames per tick,
  wrapping at `frame_count - 1`. The gait phase is therefore continuous inside
  a clip and only reseeds (at the matched entry frame) when the clip changes.
- `PlaybackState::frame_time` is the continuous clip time; `frame` is the
  rounded value kept for audit. The authority audit now requires clip identity
  for a motion-driven tick and additionally the action cursor for every action
  tick, because the action clock is not the locomotion clip-time authority.
- Baked root XY cannot move the Player: it only feeds the reference speed (and
  therefore the gait). Disabling or editing it changes gait quality, never the
  world trajectory already committed by `CommitLocomotion`.
- One-shot actions (pass/shot/slide/trip/fall/header) keep the action clock as
  their clip-time authority; `touch_frame` timing is untouched by M2.

## Next

- M3: promote the P3a warp into the single Final Pose channel
  (`reference_pose` → `final_pose`), so `Pose()`, `GetBakedPoseSample()`,
  `Geometry()` and `ContactRegions()` all read the same IK-applied skeleton.
- M4: split the Player tick into explicit action/motion/pose/contact phases and
  let Runtime constrain the candidate root before the final pose, re-solving the
  pose (or deferring the contact response) when a contact changes the root.
- P6 special actions stay action-motion driven: their body model (slide
  friction, fall impulse, shot support foot) is not the locomotion constraint
  core, and must not be collapsed into it.

## STRIDE-0..3 result: stride adaptation and the skeleton write-back

The pieces below answer "the clip wants 0.06 m this tick, the motion allows
0.04 m — where does the difference go?". Cadence and step length are two
different absorbers, and they must be applied together or the support foot
slides.

```
sampled gait  ──▶ stride target (AdaptStride) ──▶ support lock (plant)
              ──▶ local-rotation write-back (ApplyWorldFootTargetsToPose)
              ──▶ the one Final Pose (Pose(), BakedPoseSample(), Geometry())
```

### STRIDE-0: calibration

- `LocomotionClipReferenceSpeed` measures the travel over the intervals that
  exist: `N` root samples span `N-1` ticks, so the denominator is
  `(N-1) * 10 ms`, not `frame_count * 10 ms`. The old denominator overstated
  every clip's native speed by `N/(N-1)` (4 % at 25 frames) and biased the
  playback rate low. `loco_animation` now checks it against the clips' own root
  tracks: for every clip whose track is uniform (< 2 % interval spread) the mean
  and the track agree within 2 %; 86 clips are checked, worst case 0.3 %.
- `LocomotionPoseDiagnostics` records per locomotion tick: achieved distance,
  wrap-aware reference distance, playback rate, stride scale, and per-foot
  `animation_slip` (gait target vs the anchor it is still locked on) and
  `foot_slip` (residual of the shipped pose against the anchor). Nothing in
  production branches on them.

### STRIDE-1: stride adaptation

- `PlayerPoseAdapter::AdaptStride` scales only the **hip-relative fore-aft**
  component of each ankle, so the leg stays the same length and the knee is
  reconstructed by the two-bone solve. Lateral offset, foot lift and the root
  trajectory are untouched.
- `kLocomotionStrideCoupling = 0.5`: `stride_scale = (achieved/reference)^0.5`,
  clamped to `[0.6, 1.35]` and smoothed with `tau = 0.3 s`, i.e. per gait cycle
  rather than per tick. 0 would be cadence-only (the clip plays in slow motion),
  1 would be stride-only (native cadence, shorter steps).
- **The pairing invariant.** The playback rate divides by the stride scale:
  `rate = achieved / (reference * stride_scale)`. A stance foot then moves
  `stride_scale * reference * rate = achieved` backwards in clip space, which is
  exactly the root's forward motion — so it stays put in the world. Changing one
  without the other is the "double correction" failure: with `scale = 0.8` and a
  rate computed as if the stride never changed, the gait target drifts 5.4 cm
  per stance from its anchor on the synthetic run; paired, the residual is
  0.2 mm (both covered by `player_stride_pipeline_test.cpp`).

### STRIDE-2: skeleton write-back

- Support and stride are separate authorities: `ApplyFootPlantLocks` overrides a
  planted foot's target with its anchor, and `AdaptStride` never sees the plant
  state. No two systems write the same foot.
- `ApplyWorldFootTargetsToPose` turns the solved ankle targets back into **local
  joint rotations** (thigh aims at the solved knee, shin at the solved ankle, the
  boot keeps its world orientation so the sole stays flat). The FK bind table
  (`kBakedBindJoints`) is now the single description of the hierarchy, shared by
  `EvaluateBakedBodyPose` and the write-back.
- Because the write-back lives in the pose, `Pose()`, `BakedPoseSample()`,
  `Geometry()` and `ContactRegions()` all read the same skeleton; passing the
  same targets to geometry again is idempotent.

### Settle stage and support derivation

One stage per Movement tick, in `PlayerExecution::SettleMovementPose`:

1. forward-kinematics the sampled/blended pose (the *gait*);
2. stride target per leg;
3. support from the **gait** ankle heights (lower foot, 1 cm switch margin) —
   the previous `SpatialState::foot` heuristic is gone, because with
   motion-driven playback it drifts out of phase;
4. plant update, with a newly planted foot anchored at its stride-adapted ankle;
5. lock planted feet, solve, write local rotations into `final_pose`.

Support must be read from the gait, never from the settled pose: a locked foot
keeps its height by construction, so a settled-pose comparison can never release
it (that deadlock was observed and is why the stage owns both decisions).

### M3 result: one Final Pose channel

Per real playback tick, in this order:

```
playback blend            playback_state_.final_pose        (Animation)
  -> Warp                 yaw/bob toward the achieved motion (locomotion only)
  -> Realize              bounded joint rotation rates vs the previous Final Pose
  -> SettleMovementPose   stride target, support, plant lock, local rotations
  -> final_pose_          the one skeleton everything reads
```

- `PlayerExecutionState::final_pose_` (+ `final_pose_base_angle_`) is the single
  Player-corrected skeleton. `Pose()`, `PlaybackPose()`, `BakedPoseSample()`,
  `Geometry()`, `ContactRegions()`, the touch preparation and the runtime body
  tape all read it; nothing re-solves a second pose. The base yaw stays a
  separate field, so animation matching still compares local joint rotations.
- `PlayerPoseAdapter::Warp` is now in the production path for motion-driven
  locomotion: the clip's own reference displacement for the tick (wrap-aware,
  rotated by the base yaw) is compared with the achieved displacement, and the
  body yaw / pelvis bob follow the motion. One-shot actions keep their baked
  bearing.
- `PlayerPoseAdapter::Realize` is the shipped bounded rotation-rate limiter,
  moved out of Runtime. It runs *before* the foot solve on purpose: the rate
  limit smooths the incoming animation, and the stride/plant solve is the last
  word on the feet, so limiting can never unlock a planted foot. `entry_weight`
  is the clip-entry damping (1 = first frames, 0.3x the joint budget).
- Clip switches now blend from the published Final Pose
  (`PlaybackState::SetBlendSource`), so a switch starts from the skeleton that
  was actually drawn. The blend output is the *input* to the corrections, which
  are recomputed from it every tick and therefore cannot accumulate.
- Runtime's `RealizeBakedPose` is forecast-only now
  (`player_baked_pose_realization.hpp`): `CompleteBakedBodyPreparation` folds the
  base yaw and consumes the Player's pose unchanged, so the collider can no
  longer lag the pose the touch decisions and the renderer use.

### M4 result: tick order and the contact hand-back

The Player tick tail now runs in the order the pose needs, not the order the
legacy code happened to have:

```
... AdvanceActionAndMovement   motion committed (root, velocity, facing)
    SelectActionForFrame        action resolved
    ApplyMovementOffsets        smuggle + goalkeeper clamps: the last root
                                mutations the tick performs itself
    ComposePose                 sample -> warp -> realize -> settle: the pose
                                that belongs to the root above
    PrepareActionContacts       touches consume that settled pose
```

- Contacts moved **last**: the boot surface point, the pose and the root under it
  now belong to the same tick. Previously the touch stage read the previous
  tick's pose while this tick's root had already been committed.
- The locomotion classification used for the pose comes from the retainer state
  the *motion* was committed with (before this tick's touches), so the pose can
  never be produced from a different locomotion mode than the root it describes.
  A possession gain therefore takes effect from the next tick, which is also
  when its action actually runs.
- A touch can only **propose**. It never moves the root, so nothing can leave a
  settled pose stale from inside the tick.

Root corrections that arrive from outside (Runtime pair contact, ball
arbitration) happen after the tick. They go through
`Player::OffsetPosition` -> `Player::ApplyPositionCorrection` ->
`PlayerExecution::ResettleFinalPose`:

- the settle is re-run on the **same** realized pose, stride scale, gait phase
  and plant anchors - no clip time is consumed and no new sample is taken, so
  the action/touch clock cannot drift;
- the plant anchors are world-fixed, so the legs re-reach the feet that are
  still on the ground; the result is bounded by the two-bone reach and the
  residual is reported (`BakedPoseFootSolve::left/right_residual`,
  `LocomotionPoseDiagnostics::*_foot_slip`);
- an out-of-reach correction shows up as a large residual instead of a stretched
  skeleton, which is the signal a caller needs to defer the response to the next
  tick;
- `Player::Diagnostics().pose_resolves` counts them.

### Stage 1 result: consistency fixes and real-clip acceptance

Four defects found in review, plus the boundary rules they exposed:

- **Geometry no longer re-solves the pose.** `BakedPoseSample()` leaves
  `foot_targets` empty because the committed pose already carries the foot solve
  in its local rotations; `Geometry()`/`ContactRegions()` are a pure FK pass.
  `PreviewRawPose()` explicitly clears the targets, so a raw-clip preview cannot
  silently inherit the current support anchors. Measured difference between the
  two paths on the settled pose: < 2 mm (they describe the same skeleton).
- **Zero-speed playback.** The playback rate is derived, and there is no lower
  clamp: a stationary Player freezes a movement clip instead of letting it walk
  on its own, while a clip with no speed reference (idle/stance) keeps its own
  cadence so standing still does not freeze a breathing cycle. The old 0.35 floor
  made the stance foot travel faster than the root at low speed - exactly the
  slide the plant then had to absorb.
- **One reset contract.** `PlayerExecution::ResetPoseChannel()` clears playback,
  cadence, support anchors, the support-detection history and the Final Pose
  channel together. A stale anchor is a *world* position, so leaving one behind
  would pull the next pose toward where the Player used to stand.
- **Support is now the foot that travels backwards**, not the lower one
  (`SelectFootSupport`). It is clip-intrinsic: it does not depend on the playback
  rate or the achieved speed. Height alone mislabels half a cycle on authored run
  clips (measured on clip 768: the lower foot was swinging at 9.8 m/s while the
  other was in stance at 2.1 m/s), which produced a 0.58 m anchor drift and a
  1.34 rad/tick leg snap.
- **An unreachable anchor relaxes instead of pinning the leg**
  (`RelaxFootPlant`): below 6 cm residual nothing happens; above it the anchor
  slides toward the foot by at most 3 cm/tick (the foot really does move when the
  body was pushed away); above 35 cm the pose is not a support problem and the
  anchor is released. `LocomotionPoseDiagnostics` reports
  `*_plant_released`/`*_plant_slide`.
- **Measured, not hidden**: `LocomotionPoseDiagnostics::max_leg_joint_step` is the
  peak leg-joint rotation between the previous and this Final Pose. The
  realization budgets the *incoming* animation; the plant solve has the last
  word, so this is the shipped truth and it is reported rather than capped (a
  second limiter would reintroduce foot slide).

Real-catalog acceptance (`unittests/loco_animation/loco_motion_pose_test.cpp`,
clip 768 at 5.05 m/s reference, 80 ticks):

| scenario | bone error | shipped slip | IK residual | anchor slide | leg step |
|---|---|---|---|---|---|
| cruise | 0 | 0.084 m | 0.111 m | 0.03 m/tick | 0.90 rad |
| brake 5→0.75 m/s | 0 | 0.072 m | 0.099 m | 0.03 m/tick | 1.01 rad |
| stop | 0 | 0.081 m | 0.109 m | 0.03 m/tick | 0.50 rad |
| turn 90° | 0 | 0.040 m | 0.040 m | 0 | 1.31 rad |

The remaining slip is **authored into the clip**: on clip 768 the stance foot
travels ~4 m/s inside the animation while its own root track claims ~5.05 m/s, so
the plant has to absorb ~1 m/s for the whole stance. That is the material the
Baker support track (next stage) and a foot-lock rate calibration
(`rate = achieved / stance_foot_speed` instead of `/ root_track_speed`) are for.

### Stage 2 result: the baked support track and foot-lock speed

The support decision is now an animation fact, not a Player heuristic.

- **Phase track.** `FootPhase { Swing, Landing, Stance, Takeoff }` per foot per
  frame, derived from the clip's own leg poses by `DeriveFootSupport()` and
  cached on the clip when `AnimationLibrary::Load` reads it. `Landing` is the
  first contact frame, `Takeoff` the last, `Stance` everything between; the walk
  is cyclic with hysteresis (down at 4 cm above the clip's lowest ankle, up at
  9 cm), so a stance that crosses the loop boundary still gets both edges.
- **No artifact change.** The derivation is one implementation in the animation
  module (`src/animation/foot_support.cpp`), not a second one in the Python bake.
  The artifact format stays v7, and if a bake-time track is ever written the
  query API (`SampleFootSupport`) does not change.
- **Shared skeleton declaration.** The 14-bone hierarchy and its bind offsets
  moved to `football/animation/foot_support.hpp` (`kPlayerBindJoints`), because
  both the animation derivation and the Player's forward kinematics/colliders
  must read the same table.
- **Foot-lock speed.** The derivation also reports `clip.stance_speed`, how fast
  a stance foot travels backwards *inside the clip*, and `LocomotionClipGaitSpeed`
  prefers it over the root track for playback. Authored clips disagree with their
  own root track: on the shipped catalog clip 768 the stance foot moves at
  3.51 m/s while the root track claims 5.05 m/s, so a rate derived from the root
  track dragged the foot across ~1 m/s for a whole stance.
- **Guard.** Baked phases assume the clip is played along its own direction. When
  the body turns while the animation still says "stance", the foot is pivoting,
  not planting: a foot whose *world* speed exceeds `kStanceMaxWorldSpeed` (2.5
  m/s) is not locked, whatever the track says. Without that guard the 90° turn
  scenario shipped 0.146 m of slip; with it, 0.006 m.
- The Player still falls back to its velocity-based `SelectFootSupport` for clips
  without a track, and `PlayerFootIkTargets`/plant anchors behave as before.

Measured through the production chain on clip 768 (5.05 m/s root track,
3.51 m/s stance) and clip 302 (turn):

| scenario | shipped slip before | shipped slip after | leg step before | after |
|---|---|---|---|---|
| cruise | 0.084 m | **0.032 m** | 0.90 rad | 0.91 rad |
| brake 5→0.75 m/s | 0.072 m | **0.020 m** | 1.01 rad | 0.91 rad |
| stop | 0.081 m | **0.024 m** | 0.50 rad | 0.90 rad |
| turn 90° | 0.040 m | **0.006 m** | 1.28 rad | **0.44 rad** |

### What is still open

- The support track is derived at **load** time. If the pipeline ever wants it in
  the artifact, the same function can run in the bake; nothing downstream changes.
- `stance_speed` is one number per clip. A per-phase profile (landing/stance/
  takeoff) would let playback follow the foot through a step that changes speed.
- The peak leg-joint step (0.9 rad/tick on the cruise clip) is reported by
  `max_leg_joint_step` but not bounded; bounding it needs a release ramp, not
  another global limiter.
- The entry damping is driven by the action cursor (`PlayerActionState::Frame`),
  a per-clip tick counter.
- Runtime contact still resolves pairs one after another, so a chain of
  corrections in one tick re-settles once per correction instead of being
  coalesced. That is a cost, not an inconsistency.
