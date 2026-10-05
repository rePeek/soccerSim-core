# 5a1 — Measure animation-owned contact authority (no migration)

**4f Movement migration is closed at its stated boundary:** direct pure-locomotion animation-command reads have been removed. This is not a claim of animation-independent gameplay: 4f-c showed that changing animation duration changes action lifecycle and gameplay. Contact authority is a separate migration; 5a1 only observes Shot, ShortPass, LongPass, HighPass, Trap and BallControl. It changes neither selection nor contact execution, Player Decision queries, controller calls or random draws.

Current-tree naming: `PlayerBase::BeginSimulationAction` below is now
`Player::BeginSimulationAction`. Flattening removed the base class, not the
contact/animation authority described by this historical measurement.

## Ownership/dependency map

`PlayerBase::BeginSimulationAction()` copies the accepted animation's frame count, `touchFrame` and `touchPos` into the simulation action definition. This makes the **cursor** simulation-owned, not the **initial timing or geometry**. At `Humanoid::Process()`'s scheduled-contact frame, execution still reads `currentAnim.touchPos + positionOffset`, the animation extension's `GetTouchPos(contactFrame)` (height), `incoming_retain_state` (distance override), `touch_bodypart` (touch classification) and the live ball state. The gate requires distance < 0.4 m (1.0 m for incoming retain) and height difference < 1.0 m. `bumpyRideBias` is derived from the distance and can modify the outgoing vector. Only a passed gate can request `Ball::Touch` for these six types. The action can be interrupted before reaching its contact frame. The simulation action's copied `contactPosition` does **not** replace the `currentAnim` geometry read at execution.

| Action | Post-gate vector authority (in addition to common animation timing/geometry/bodypart and live ball/kinematics) | Animation-specific inputs / conditional behavior |
| --- | --- | --- |
| Shot | `GetShotVector`: accepted `originatingCommand.touchInfo` direction/power and desired velocity, animation-position cache/movement at frame, player stats, live ball, randomized inaccuracy/spin. | `touch_maxpowerfactor` caps power; shot angle and shot power depend on selected animation movement. |
| ShortPass | Accepted `originatingCommand.touchInfo` direction/power/target/forced target; `AI_GetPass` may refine target at contact; live controller direction may be read. | If `_PassFiddlingEnabled()`, `GetBestPossibleTouch` reads animation `touch_maxpowerfactor`, `touch_difficultyfactor` and `balldirection` (rotated by the animation's start/smuggle angles); difficulty factors and the no-native-direction branch draw randomness. |
| LongPass | Same pass command and target refinement; different type-specific pass trajectory/power. | Same conditional pass-fiddling and animation profile inputs as ShortPass. |
| HighPass | Same pass command and target refinement; different type-specific trajectory. | Same conditional pass-fiddling path; absence of a profile key means its default, **not** independence from that key. |
| Trap | `GetTrapVector` → `GetBallControlVector`: accepted desired direction/velocity plus live controller float velocity, outgoing animation movement/velocity/frame count, spatial and live ball state. Difficulty factors include an existing random draw. | No pass/shot profile power/difficulty input; animation outgoing velocity and remaining duration affect control. |
| BallControl | `GetBallControlVector`: same command, live controller velocity, outgoing animation movement/velocity/frame count, spatial and ball inputs; optional KnockOn branch uses command modifier. | Control vector depends on selected animation's outgoing motion and duration. |

See `src/sim/player/humanoid/humanoid.cpp` for the gate/branch and `humanoid_utils.cpp` for the Shot, Trap and BallControl vector helpers. Profile **presence counters** below are not an assertion that a value was non-default, or that a conditional profile branch ran. Likewise `command_target` counts a non-null accepted target, not necessarily the final target after `AI_GetPass`.

## Fixed-seed observation

The default-disabled, transient audit is enabled for one fresh match in `MeasureContactAuthority` after other regression cases; it records each accepted scheduled contact and the actual contact-frame path, then disables itself. No extra `SelectAnim()`, controller request, or RNG consumption is introduced. Counter deltas, quantiles and stage-balance assertions are reported separately for all six types. Run `GFOOTBALL_DATA_DIR=$PWD/data /tmp/fc-b-build/football_regression` (or the CTest `football_regression` case).

| Action | Scheduled | Reached frame | Geometrically reachable | Distance/height rejected | `Ball::Touch` calls / nonzero requests | Scheduled frame p50/p90 (10 ms/frame) | Request speed p50/p90 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Shot | 2 | 2 | 2 | 0/0 | 2/2 | 17/17 | 13.08/13.08 |
| ShortPass | 10 | 9 | 8 | 1/0 | 8/8 | 31/38 | 13.21/18.12 |
| LongPass | 13 | 13 | 13 | 0/0 | 13/13 | 31/36 | 16.39/20.71 |
| HighPass | 12 | 12 | 11 | 1/0 | 11/11 | 29/41 | 15.77/42.82 |
| Trap | 23 | 18 | 18 | 0/0 | 18/18 | 30/36 | 3.57/6.35 |
| BallControl | 75 | 67 | 65 | 2/0 | 65/65 | 14/22 | 5.19/8.73 |

This corpus also saw 1 incoming-retain override on ShortPass; 8/13/11 executed pass-fiddling branches for Short/Long/HighPass. All 121 reached-frame contacts had an animation position buffer, while 34 had nonzero `positionOffset` (1 Shot, 6 ShortPass, 7 LongPass, 3 HighPass, 3 Trap, 14 BallControl). Shot sample size is especially small, no height rejection or reachable-but-no-impulse path was exercised, and `scheduled - reached` includes interrupts and right-censoring at the corpus boundary. Values are descriptive, **not** a universal reachability guarantee. A `Ball::Touch` call records a requested impulse, **not** proof that ball physics adopted it or that the ball's eventual motion changed. The normal golden snapshots remain unchanged; no golden regeneration is needed for diagnostic-only additions.

## Next edges (not part of 5a1)

Migrate and verify Pass contact authority first (including target/controller refinement and conditional modifiers), then Shot (randomized shot vector/profile), then Trap/BallControl. For each edge separately attribute semantic changes and check save/load and gameplay before considering a golden update; keep ordinary unperturbed results exact for measurement-only changes. Do not claim lifecycle independence merely because locomotion reads decision intent rather than an animation command.
