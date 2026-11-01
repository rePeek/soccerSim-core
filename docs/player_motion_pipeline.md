# Player × Animation: reference motion → adapted root → final pose

Status: RM-0..RM-9 and RM-F1..RM-F4 are implemented. Runtime foot IK is
removed; import-time FootSupport remains for gait repair, quality analysis
and the IK-free source gate. Historical sections below that mention
`PlayerFootIk` / `AdaptStride` describe stages that no longer exist.

## Target chain

```
PlayerControl / intent
  → Animation::Match        (standard clip + legal entry frame)
  → SampleAnimationStep     (clip root delta + pose on one timeline)
  → PlayerActionAdapter     (body, capability, bounded playback, turn check)
  → Adapted root            (the only committed world motion)
  → Pose adapter            (Warp -> bounded Realize; no foot lock/IK)
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

- A1b: the offline `football_anim_baker --autogen-out` mode regenerated the
  443 non-mirror Autogen clips from the 10 legacy templates.
  `autogen.simanim` was passed to the Python catalog bake as `--autogen`.
- The generated Autogen poses differ from the removed GLBs by at most
  `7.45e-9`; roots are identical. Stored metadata is retained and validated
  within `2e-5`.
- A2: `assets/animations/` contains 283 authored GLBs + manifest. 443
  entries are `procedural`, 726 are `mirror_of`.
- Superseded: the generator now reads `assets/motion_templates/*.glb` in C++
  and runs at catalog load time (`src/animation/library.cpp`).
  `tools/animBaker` and the legacy `.anim`/`player.object` inputs are gone.

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

### ACFG-0..2 result: shared clips, per-player configuration

Three kinds of data are now separate, which is what keeps the Animation module
simple:

| kind | owner | lifetime |
|---|---|---|
| `AnimationClip` (+ derived `foot_support`, `stance_speed`) | `AnimationLibrary`, one per process | immutable, shared |
| `PlayerAnimationConfig` (one `ClipTuning` per clip) | the Player, built in its constructor | constant for the match |
| `PlayerAnimationState` (playback time, stride scale, plant, `final_pose_`) | `PlayerExecutionState` | per tick |

- `AnimationStyle` (`include/football/player/player_animation_style.hpp`) is the
  user-facing input: stride/cadence preference, stride response, pelvis-bob scale
  plus two reserved pose scales. Presets are Natural / LongStrideLowCadence /
  ShortStrideHighCadence; `PlayerDefinition::animation_style` is optional, so the
  shipped data is unchanged (neutral style).
- `BuildPlayerAnimationConfig(definition, body, library, overrides)` runs once in
  the Player constructor and produces a `const PlayerAnimationConfig` inside the
  execution. It copies no skeleton frame (~6 floats per clip). Tuning is
  sanitized (clamped, NaN-safe) and deterministic. Overrides are keyed by the
  clip's **stable name**, never by numeric id (ids follow load order).
- `ResetPoseChannel()` clears playback/support/final-pose state and cannot touch
  the configuration: it is `const`, so an action switch, a reset or a fall cannot
  lose a player's style.

Stride/cadence math (`LocomotionStrideScaleFor`):

```
q    = A / (R * S0 * C0)      A achieved, R clip gait speed, S0/C0 the player's preference
S    = clamp(S0 * q^lambda)   lambda = stride_response
rate = A / (R * S)
```

At the player's preferred speed `A = R*S0*C0`, the stride is exactly `S0` and the
derived rate is exactly `C0` - the two preferences mean what they say. With the
neutral style the formula is algebraically identical to the shipped
`(A/R)^0.5`, which the tests assert bit-for-bit against the old entry point.

One effective gait speed: `LocomotionClipGaitSpeed(clip) * body.height_scale`
drives both the stride target and the playback rate (root track only as a
fallback). Clip *selection* deliberately keeps the root-track definition, because
the coarse candidate filter runs on the baked velocity metadata - ranking by a
different speed definition collapses the walk/sprint buckets (measured: a walk
and a sprint query both selected clip 566).

Acceptance (same clip, same 4 m/s, two styles, 60 ticks):

| | world distance | shipped slip | bone error | cadence |
|---|---|---|---|---|
| short stride / high cadence | 2.40 m | 0.010 m | 0 | higher |
| long stride / low cadence | 2.40 m | 0.061 m | 0 | lower |

The world displacement is identical because the animation cannot move the Player;
only the gait differs. The long-stride residual is larger because the requested
step is close to the leg's reach - reported, not hidden.

Reserved: `torso_sway_scale` / `arm_swing_scale` are carried by the configuration
but not applied yet; a blanket joint-rotation scale would distort the gait, so
they need a per-joint, pose-relative implementation (and visual acceptance).

### ACFG-3/4 result: decisions moved to the Player, Animation slimmed

The action *decision* is Player code now; Animation is the shared clip resource
plus the pure algorithms that turn a clip into data.

| moved (git mv, code unchanged) | from | to |
|---|---|---|
| `Match`, `MatchQuery`, `MatchResult`, `MatchBranch` | `animation/match.*` | `player/execution/player_action_resolver.*` |
| candidate refinement, body/velocity direction snap, idle/retain picks | `animation/movement_selection.*` | `player/execution/player_action_candidates.*` |
| `ResolveMovementPick` | `animation/select_movement.hpp` | folded into `player_action_resolver.hpp` |
| `MatchTouch`, `TouchMatchContext/Queries/Result`, traces | `animation/touch_match.*` | `player/execution/player_touch_action_resolver.*` |
| smuggle/body-ball scoring | `animation/touch_scoring.*` | `player/execution/player_touch_action_scoring.*` |
| `SelectTripMatch` | `animation/trip_match.*` | `player/execution/player_reaction_resolver.*` |

The moved code sits in `namespace football::player` and keeps using Animation's
pure helpers explicitly (`using namespace football::animation;`), so the
algorithms are untouched: this is a relocation, not a rewrite.

Stays in Animation (by design): `SamplePose`, `BlendPose`, `SelectEntryFrame`,
`PoseDistance`, `AnimationTransitionState`, `PlaybackState`, the foot-support
derivation and touch geometry. Candidate selection (the crude filter, its query
and its quadrants) is Player code since R2; `Quadrant` stays here as a value type
because the baker's legacy import assigns it.

Build targets: `football_animation` lost the six decision translation units;
`football_player_execution` gained them. `football_animation_base` still holds the
format, clip, library, foot support and selector, so the offline baker keeps
linking only that archive.

Tests: the six decision suites moved to a new `player_decision` target (links
Player Execution and needs the baked cache), because they now test Player-owned
code. They are unchanged apart from the include/namespace. `unittests/animation`
keeps only the resource and pure-algorithm suites.

Known leftovers for a later pass: `select_clip.hpp` has no production consumer
(removed in R2) and `BakedAnimationSelector`'s legacy interface (moved to the
Player in R2).

### ACFG-FIX / STRIDE-REACH / FOOT-CONSISTENCY

Three defects from review, plus the measurement that decides what is next.

- **One speed scale.** `AdvanceLocomotionPlayback` no longer derives a reference
  speed internally: `LocomotionEffectiveGaitSpeed(clip, height_scale)` is computed
  once by the caller and passed in, so the stride target and the playback rate
  cannot be paired against different scales (previously the stride used
  `gait_speed * height_scale` and the rate used `gait_speed`).
- **Pelvis bob style always applies.** `Warp` used to return early when the yaw
  correction and the bob ratio were both neutral, which silently dropped
  `pelvis_bob_scale != 1`. The style scale is now part of the decision.
- **Reach-aware stride, before the pairing.** `ClipTuning::max_stride_scale` is
  computed once per player and clip in `BuildPlayerAnimationConfig`: walk the
  clip's own poses, take each leg's hip-relative ankle offset, and keep the
  tightest `S` satisfying `(S*f)^2 + |r|^2 <= (thigh+shin)^2` (2% safety margin).
  The executor clamps the style's target stride with it *before* smoothing and
  before deriving the rate, because clamping after the rate would break the
  pairing and reintroduce foot slide. `AdaptStride` also clamps per leg when a
  caller supplies `reach`, and reports `applied_stride_scale`/`reach_limited`.
- **Final Pose matches the plant state it ships with.** When `RelaxFootPlant`
  moves or releases an anchor, the pose is solved once more against the new
  anchors (bounded: only on relaxing ticks). Previously the tick returned a pose
  solved for the anchor that had just been abandoned.
- **Residual provenance.** `LocomotionPoseDiagnostics` now separates
  `max_plant_residual` (a locked support anchor the leg cannot reach) from
  `max_swing_residual` (a swinging foot's own target out of reach). They mean
  different things and are no longer conflated.

That separation answers the open question about the 0.061 m residual: across the
whole acceptance matrix it is **100 % plant-side** (`swing = 0.000` everywhere),
i.e. the clip's baked stance-foot disparity drifting the anchor away from the
foot - not an over-long stride request. Reach-aware stride therefore protects the
swing target and bounds the style, but it does not remove that residual; the
foot-lock playback speed is what reduced it, and the next step for the rest is the
baked support/contact contract, not a stricter stride clamp.

Acceptance matrix (`loco_motion_pose_test`, clip 768, 4 m/s, 40 ticks, each
variant driving its own skeleton):

| variant | plant residual | swing residual | bone error | anchor slide |
|---|---|---|---|---|
| natural / 1.62 m | 0.085 m | 0 | 0 | 0.025 m |
| natural / 1.80 m | 0.058 m | 0 | 0 | 0 |
| natural / 1.98 m | 0.021 m | 0 | 0 | 0 |
| short steps / 1.80 m | 0.004 m | 0 | 0 | 0 |
| long steps / 1.80 m | 0.086 m | 0 | 0 | 0.026 m |
| long steps / 1.98 m | 0.066 m | 0 | 0 | 0.006 m |

Every variant covers the same 1.60 m of ground: height and style change the gait,
never the trajectory. Taller players have smaller residuals because their legs
reach the drifted anchor for longer.

### P2 result: the support contract and its quality gate

The derivation is now a contract, not just a per-frame flag:

| what | where | meaning |
|---|---|---|
| `FootSupportFrame { FootPhase left, right }` | per frame | Swing / Landing / Stance / Takeoff |
| `FootSupportSummary` | per clip | per-phase stance speed, support/double/flight duty, shortest contact run, quality flags |
| `clip.stance_speed` | per clip | the stance-phase mean, the cadence reference playback follows |
| `SampleFootSupport(clip, frame_time)` | query | nearest baked frame, clamped, wrap-aware |
| `FootSupportTrusted(summary, has_track)` | verdict | may the Player lock feet from this track? |

Quality flags: `no_support` (never carries weight), `all_support` (both feet down
for the whole cycle - a stance, not a gait), `stance_too_short` (a contact run
shorter than two frames is not a step), `speed_outlier` (the stance foot disagrees
with the clip's own root track by more than 1.6x + 1 m/s). An untrusted clip does
not drive cadence and does not lock feet: the Player falls back to the
velocity-based `SelectFootSupport` and to the root-track speed. That is the whole
point of deriving per phase instead of averaging: a wrong reading must be
detectable.

Measured on the shipped catalog (1016 Movement clips): 876 trusted (86%),
66 `all_support`, 72 `speed_outlier`, 2 `stance_too_short`, 0 `no_support`. The
gate is accepted at >= 80% and the clips the locomotion pipeline actually selects
are asserted individually to be trusted.

**Why the artifact stays v7.** Writing the track into `.simanim` would put the
derivation in two places: Python builds the 283 authored clips, the C++ baker the
443 Autogen ones, and only the C++ side shares `DeriveFootSupport`. Serializing
today's vector would bake a guess, not a contract, and the user's own criterion is
that the format change must buy correctness. A bake-time contract that is worth a
version bump needs (a) one implementation reachable from both paths - e.g. a C++
"support pass" that rewrites the finished artifact - and (b) foot contact points,
which are a *model* fact (boot geometry) that Animation does not own today. Until
both exist, load-time derivation keeps one implementation and zero duplication.

### R2/P3 result: the crude filter leaves Animation, legacy verification deleted

| moved (git mv, code unchanged) | from | to |
|---|---|---|
| `BakedAnimationSelector::CrudeSelection` | `animation/selector.cpp` | `player/execution/player_crude_filter.cpp` as `CrudeAnimationFilter::Select` |
| `CrudeSelectionQuery`, `SelectionProperties` | `animation/selection_query.hpp` | `player/execution/player_crude_query.hpp` |

`Quadrant` stays in Animation: it is a pure value type the offline baker's legacy
import still assigns, not a decision.

Deleted as legacy (no behaviour change to the game):
- `AnimCollection::CrudeSelection` in the baker (326 lines) - the tool's own copy
  of the legacy selection, used only by the deleted verification;
- the baker's query matrix + `VERIFY-SELECTION` mode (182 lines + the CLI flag +
  the wrapper): it proved that the baked selection matched the legacy one during
  A0/A1. That migration is complete; re-proving it on every run is not a test of
  today's code.

Measured: Animation went from 1789 to **1437** non-comment code lines (-20%),
and the baker lost 525 lines. Runtime cost: zero (pure relocation).

What is left in Animation, by code lines: artifact schema + loader 390, enums and
value types 186, foot support 246, pose/playback/transition 230, touch geometry
169, reference motion + entry frames + math 215.

The only legacy left is inside the artifact *schema* itself: 9 metadata fields are
read by nobody (`forced_foot`, `special_var1/2`, `touch_foot`,
`incoming_movement`, `incoming/outgoing_ball_direction_max_deviation`,
`incoming_ball_direction`), plus ~12 that only one to three call sites touch.
Removing them needs a `.simanim` v8 bump and a matching Python writer change, for
about 40 lines of saving - which is why it is queued behind a real contract change
rather than done on its own.

### What is still open

- Support phases are derived at load time. A bake-time contract needs one shared
  implementation for both bake paths plus boot contact geometry; until then a
  format bump would only move the same guess earlier.
- `torso_sway_scale` / `arm_swing_scale` are carried but unapplied.

- The remaining plant-side residual is the clip's baked stance-foot disparity
  plus the anchor drift it causes. Removing it needs the baked support **and
  contact** contract (per-phase stance speed, foot contact point), not a tighter
  stride clamp - the diagnostic shows the swing target is never the problem.
- `torso_sway_scale` / `arm_swing_scale` are still carried but unapplied.

- The artifact schema still carries 9 metadata fields with no reader and ~12 with
  one to three call sites. Removing them needs a `.simanim` v8 bump plus a Python
  writer change, for ~40 lines of saving.

- ACFG-3: move the action *decision* (`Match`, `MatchTouch`, `SelectTripMatch`)
  into the Player. Animation keeps `SamplePose`, `BlendPose`, `SelectEntryFrame`
  and `PoseDistance`. The touch matcher must move together with its execution
  semantics (touch timing, ball prediction, contact geometry), not be replaced by
  a `FindByType(Shot)`.
- ACFG-4: once the production callers are migrated, slim the Animation build
  targets. The crude filter and its query now live in the Player, so the only
  selection-shaped value left in Animation is `Quadrant`.
- A stride preference near the leg's reach is bounded by the IK, not by the
  configuration. A reach-aware clamp inside `LocomotionStrideScaleFor` would keep
  an aggressive style from asking for a step it cannot make.
- Torso/arm pose scales are reserved (above).

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

## RM-0..RM-8 result: the GLB root becomes the ordinary motion authority

The earlier stages left two motion authorities alive: `PlayerMotionAdapter`
integrated ordinary locomotion while the animation only followed it. RM changes
the ordinary path so the clip's own root is the displacement source.

- **RM-0** — `AnimationLibrary::QualityCensus` now counts each movement-clip
  failure reason (untracked, untrusted support, loop discontinuity, stance
  misalignment, support slip, slow root/stance) alongside the pass counts. The
  audit test requires every movement clip to be either usable or explained.
- **RM-1** — the importer repairs straight half-cycle + mirror pairs. It builds
  the full cycle (pose seam), derives the root from the clip's own stance foot
  via FK, and retimes the cycle by stance-foot travel to a constant-speed root.
  The repaired cycle replaces the source only if the repaired cycle itself
  passes `ik_free_usable`; otherwise the authored track is untouched and the
  compatibility fallback remains. The positive acceptance now requires at least
  one real GLB gait, and a negative test still proves a bad clip is rejected.
- **RM-2** — `SampleAnimationStep(clip, t0, t1)` returns the local root delta
  and the pose from one interval. Looping clips measure the root on an
  unwrapped timeline, so crossing the seam advances instead of jumping back.
- **RM-3** — `PlayerActionAdapter` ordinary locomotion samples that root track,
  scales it by the body and base yaw, and chooses the playback rate from the
  profile's next speed bounded by the clip's playable range. Direction, loop
  phase and foot timing come from the clip; a sample that cannot be performed
  returns `NeedAnotherClip`/`CapabilityLimited` instead of being stretched.
- **RM-4** — the production path commits the adapted root and uses the gait's
  reported `playable_speed` to brake a body that is still above the clip's
  range. Acceleration/braking stay inside the motion profile; the real-GLB
  acceptance runs acceleration, steady state, loop seam and braking.
- **RM-5** — action selection moved to the end of the tick. The tick that ran
  keeps one clip for motion, pose and contacts; a newly accepted action starts
  contributing root and pose on the next tick.
- **RM-6** — a discrete action's root, pose and action clock are pinned to the
  same sample, so adaptation cannot move a touch frame.
- **RM-7** — an external root correction is recorded as
  `MotionAdaptationDiagnostics::passive_correction`; the final pose is
  re-derived from the same reference sample, and `active + passive == final`
  is asserted where the final delta is measured.
- **RM-8** — the 14-bone contract moved to `animation/skeleton.hpp`. The
  foot-support track/quality report remains for verification, selection and the
  compatibility cadence; it is not a runtime pose authority, and the old foot-IK
  helpers are no longer in the production pose path.

Known limits of this first root-driven version:

- The repaired cycle is a constant-cadence gait. A clip whose authored step
  length does not match the control speed is normalised by the playback rate;
  the spatial direction, phase and foot placement are the clip's.
- A direction the selected clip cannot express (for example a sharp turn while
  the old movement clip is still installed) returns `NeedAnotherClip` and the
  compatibility path carries the transition until a suitable clip is selected.
- Foot support is still derived from ankle height; foot roll and toe contact
  remain an offline verification concern, not a runtime IK correction.

## RM-F1..RM-F4 result: phase, final pose, turning and repaired-gait validation

The RM migration made the clip root the active motion authority. The follow-up
review found four engineering gaps; this stage closes them without changing the
architecture again.

- **RM-F1 — one phase.** The root-driven path and the compatibility fallback now
  advance the same unwrapped cursor (`locomotion_continuous_time_`). The
  fallback computes its wrapped playback time as before, then advances the
  continuous cursor by the same wrap-aware delta
  (`AdvanceContinuousLocomotionTime`). Switching back to the root path no longer
  re-samples a stale phase. `ReferenceAnimationSample::looped` now means "this
  interval crossed a seam", with `cycle_index` carrying completed cycles. The
  root/fallback/root acceptance crosses a loop seam and checks cursor monotonicity
  and pose continuity.
- **RM-F2 — final pose.** The IK-free acceptance now drives
  `SampleAnimationStep -> Warp -> bounded Realize -> Final Pose` and measures the
  stance ankle in the final world skeleton for two body heights. Steady stance
  keeps a tight budget; a support handover gets an explicit transient budget
  because a knee/ankle may legitimately turn fast while the other foot takes
  over. Realize is still the bounded production stage, not removed or bypassed.
- **RM-F3 — turning and reselection.** A root sample now checks both the
  speed-vector constraints and the heading change against
  `PlayerMotionProfile::turn_rate` (speed-scaled). A heading change beyond one
  tick's authority returns `NeedAnotherClip`/`turn_limited`. A rejected
  root-driven sample asks the selector for one replacement
  (`RequestLocomotionRequeue`), with a 10-tick cooldown so a long turn or a body
  outside the gait range cannot run candidates every tick. The fallback still
  supplies this tick's legal displacement; the replacement installs on the next
  tick through RM-5.
- **RM-F4 — repaired-gait downstream validation.** A repaired cycle changes its
  frame count and native root speed. The importer now clamps declared
  `incoming_velocity`/`outgoing_velocity` and `incoming_movement`/`outgoing_movement`
  to the repaired root's reachable range (native root speed × bounded playback),
  and rewrites `metadata.translation` from the repaired track. A catalog test
  checks frame count, no contact data on movement clips, reachable metadata,
  translation, and source/mirror symmetry. The acceptance also continues to run
  the repaired clip for many ticks without an unexpected reselection.

Remaining known boundary: the final-pose handover budget is a separate, bounded
transient standard rather than the steady-stance standard. Foot roll/toe contact
is still an offline validation concern, not runtime IK.
