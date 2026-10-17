# P21.0 — Eliza migration audit

Baseline: `88f5677`. This document is the P21.0 deliverable: it freezes the
current AI behaviour and maps the historical Eliza algorithms onto the public
`WorldState` + `TacticalBoard` → `PlayerControlSet` protocol. It changes no
code.

## 1. Frozen baseline

- Runtime baseline: A17 determinism digest `5bef607ddd567a09`, Debug and
  Release both 10/10 CTest, Core-only (`BUILD_TESTING=OFF`) and the
  public-header smoke build green.
- AI baseline: `football::ai::DefaultAI` (`include/football/ai/default_ai.hpp`,
  `src/ai/default_ai.cpp`) is the only production policy, constructed by
  `GameEnv::Start()`. It owns its two `TacticalBoard`s, is stateless across
  ticks, and writes exactly one `PlayerControl` per active `WorldPlayerState`
  in `world.players` order.
- The P21.1 equivalence test compares `DefaultAI::Update(world, out)` with
  `AIEngine::Update(world, out)` field by field on the same synthetic worlds,
  so "Default is unchanged" is a machine-checked claim, not a review claim.

## 2. Decision boundaries

| Responsibility | Owner after P21 |
| --- | --- |
| Off-ball positioning, pressing, run timing | AI (`IPlayerPolicy`, Eliza off-ball) |
| Pass target choice, shoot/dribble/tackle intent | AI |
| Goalkeeper positioning and save intent | AI |
| Formation width/depth, pressing scheme, marking targets | Coach AI → `TacticalBoard` |
| Actual kick direction/power, touch geometry | Player (`BuildPlayerCommands`, `kick_targeting`) |
| Action feasibility, arbitration, install, contact | Player |
| Body capability, fatigue decay, motion | Player / PlayerMotion |
| Offside line, keeper hands, restart permission | Rules |

Hard rule: no historical `Match*`, `Team*`, `Player*`, `Humanoid*` or
`MentalImage*` decision dependency is restored. AI reads only public
Observation values and writes only `PlayerControlSet`.

## 3. Historical sources

| Commit | What |
| --- | --- |
| `d1418b7` | `src/ai/eliza_controller.{hpp,cpp}` (1123 lines), `src/ai/team_ai_controller.{hpp,cpp}` (1212 lines), `src/ai/strategies/offtheball/{default_def,default_mid,default_off,goalie_default}` |
| `7dd3c63` | Extracted the pure algorithm bodies out of the `AI_*` bucket into `formation_policy`, `dribble_policy`, `positioning`, `player_query`, `reachability`, `offside`, `kick_targeting`, `ball_approach` |

The `7dd3c63` extraction is the important one: most of the numerical
algorithms no longer need to be re-derived, they only need an Observation-fed
entry point. Several already exist in the current tree in a different home
(see §5).

## 4. Function-level map

### 4.1 `ElizaController` (per-player policy)

| Legacy function | Real responsibility | Data it read | New input | Legacy output | New `PlayerControl` field | Migratable now? |
| --- | --- | --- | --- | --- | --- | --- |
| `RequestCommand` | Per-tick dispatch: stand-still / set-piece / off-ball / on-ball / keeper / pressure | `Match`, `Team`, `Player`, `MentalImage` | `WorldState` + `TacticalBoard` | ordered `PlayerCommandQueue` | `move_direction`, `desired_speed`, `look_at`, `action`, `target_player`, `target_position`, `power` | yes, as the `IPlayerPolicy::Update` skeleton |
| stand-still branch | Hold position, face ball, decay velocity | `IsInPlay`, `lazyPlayer`, ball retainer, floating velocity | `world.in_play`, `player.lazy`, `world.ball_retainer` | Movement command | movement fields | yes |
| celebration branch | Post-goal celebration | goal-scored flag | sim event / not in Observation | Movement command | (out of scope; Player/Runtime) | no, not needed |
| set-piece / retainer branch | Choose restart action and release | set-piece type, taker, ball retainer, RNG | `in_set_piece`, `restart`, `restart_taker`, `ball_retainer` | pass/shot command + movement | `action`, `target_player`, `target_position`, `power` | yes, but RNG in legacy must not be ported into AI |
| off-ball role branch | Dispatch to defense/midfield/offense strategy | formation role | `PlayerDirective.role` | movement command | movement fields | yes |
| `GetOnTheBallCommands` | Pass target scoring, shoot decision, panic, dribble | team tactical situations, opponent images, possession duration, stats, RNG | `WorldState` + `TacticalBoard` + Observation extensions | ordered pass/shot/dribble commands | `action`, `target_player`, `target_position`, `power`, movement | partly; see gaps |
| `_AddPass` | Emit one pass command with auto direction/power | target player, pass type | — | pass command | `action` + `target_player` | yes (actual params stay in Player) |
| `_AddPanicPass` | Emergency clear candidate list | direction, side | — | 3 ordered commands (HighPass/Shot/LongPass) | single `action` only | no; single-action API cannot express the ordered fallback |
| `_GetPassingOdds` | Line-of-sight danger score for a pass/shot target | opponent positions/velocities, origin, constants | `WorldState.players` + ball prediction | float | (drives target selection) | yes with a ball-prediction helper |
| `GetSupportPosition_ForceField` | Attraction/repulsion support position | ball predictions, closest mates/opponents, offside line, player images | `WorldState` + offside query + ball prediction | `Vector3` | `move_direction`/`desired_speed` | yes after Observation extension |
| `GetLazyVelocity` | Fatigue/work-rate velocity shaping | fatigue factor, workrate stat, average velocity, team possession, role | Observation: fatigue, role, workrate, possession | float | `desired_speed` | after Observation extension |
| `GetDirection`, `GetFloatVelocity` | Last published intent | internal | — | values | — | internal |
| `Reset` | Clear per-player memory | — | — | — | — | yes |

### 4.2 `PlayerController` base helpers (shared by Eliza)

| Legacy function | Responsibility | New home |
| --- | --- | --- |
| `AddDefensiveComponent` | Push desired position away from opponents / toward own goal | AI helper (pure, value input) |
| `GetDefendPosition` | Stand between opponent and own goal | AI helper |
| `NeedDefendingMovement` | Whether defending needs movement | AI helper |
| `OppBetweenBallAndMeDot`, `CouldWinABallDuelLikeliness` | Situational predicates | AI helper |
| `_KeeperDeflectCommand` | Emit save/deflect candidate | Player owns execution; AI emits `ControlAction::Save` |
| `_BallControlCommand`, `_TrapCommand`, `_InterfereCommand`, `_SlidingCommand` | Emit ordered discrete-action candidates | Player arbitration; AI emits a single `ControlAction` |
| `_MovementCommand` | Build movement command, optionally with to-ball assistance | AI emits movement; Player keeps the approach adaptation |
| `_CalculateSituation` | Derive possession/time-to-ball team facts | derives from Observation |

### 4.3 `TeamAIController` (team level)

| Legacy function | Responsibility | New owner | Migratable now? |
| --- | --- | --- | --- |
| `CalculateSituation` | Team possession / time-to-ball summary | AI reads Observation team facts | after Observation extension |
| `GetAdaptedFormationPosition` | Static + dynamic formation anchor | `TacticalBoard` + AI formation helper | yes (port as a value function) |
| `CalculateDynamicRoles` | Role adaptation over the squad | Coach → `TacticalBoard` | yes, board production |
| `CalculateMarkingQuality`, `CalculateManManMarking` | Man-marking assignment | Coach → `TacticalBoard.marking_target` | yes, board production |
| `ApplyOffsideTrap` | Keep the line behind the offside trap | Rules (`offside`) | yes, value query |
| `ApplyAttackingRun` | Time-boxed attacking run intent | Coach/tactical board | yes |
| `ApplyTeamPressure` | Time-boxed team press intent | Coach/tactical board | yes |
| `ApplyKeeperRush` | Keeper rush intent | AI keeper logic | yes |
| `PrepareSetPiece` | Restart planning | Rules / Runtime | no AI dependency |
| `UpdateTactics`, `Reset` | Live tactic multipliers | Coach/tactical board | yes |

The historical `TeamAIController` is **not** moved wholesale into
`ElizaPolicy`. Its per-player consequences become tactical input or AI
helpers; its rules (offside, set pieces) stay in Rules/Runtime.

### 4.4 Off-ball strategies

| Strategy | Responsibility | New input | New output |
| --- | --- | --- | --- |
| `DefaultDefenseStrategy::RequestInput` | Defensive shape + support blend + defensive component + offside trap | static/dynamic formation, fading possession, mindset, opponent distances | `move_direction`, `desired_speed` |
| `DefaultMidfieldStrategy::RequestInput` | Midfield support + optional attacking run | same + attacking-run intent | `move_direction`, `desired_speed` |
| `DefaultOffenseStrategy::RequestInput` | Forward support/run | same | `move_direction`, `desired_speed` |
| `GoalieDefaultStrategy::RequestInput` | Keeper positioning / rush | ball prediction, goal geometry | `move_direction`, `desired_speed` |
| `GoalieDefaultStrategy::CalculateIfBallIsBoundForGoal` | Ball-bound-for-goal test | ball trajectory + goal | bool → `ControlAction::Save` intent |

All four only produce movement (the keeper also gates a save intent), so they
map cleanly to `move_direction` / `desired_speed` / `look_at`.

## 5. Already-extracted primitives in the current tree

| Legacy | Current home | Notes |
| --- | --- | --- |
| `_GetPassingOdds`-adjacent kick targeting | `src/player/kick_targeting.{hpp,cpp}` | Player-side; AI must not duplicate |
| Reachability estimate | `src/player/reachability.{hpp,cpp}` (`ComputePlayerReachability`) | P19.4 pure value, reusable as Observation source |
| Free-space estimate | `src/player/perception.{hpp,cpp}` (`CalculateFreeSpace`) | already a pure function |
| Offside geometry | `src/referee/offside.{hpp,cpp}` | Rules; needs a read-only query for AI |
| Formation anchors | `src/runtime/formation.cpp` (`BuildFormation`), `TacticalBoard` | runtime initialisation, not live AI shape |
| Force-field integration | `positioning` in `7dd3c63` (not in current tree) | to be re-added under `src/ai/` |

## 6. Observation gaps (P21.2 candidates)

Ordered by the migration batches that need them.

1. **Off-ball batch (P21.3)**
   - role / formation role per player (`PlayerDirective.role` already exists, but
     the *dynamic* role does not);
   - ball prediction at 200/350/500/650 ms (pure helper or value);
   - offside line x for the defending team (Rules value);
   - closest-mate/opponent selection is derivable from `WorldState.players`.
2. **On-ball batch (P21.4)**
   - possession duration;
   - player stats needed by scoring (short-pass technical, shot technical);
   - panic inputs (team possession amount, goal closeness).
3. **Keeper / tactics batch (P21.5)**
   - fatigue summary (`GetFatigueFactorInv`, average velocity);
   - team possession amount / fading possession;
   - time-to-ball summary for both teams.

Constraints unchanged: no `Player*`, no `PlayerActionState`, no full
`MentalImage`, no `PlayerDefinition` copy, no re-implementation of
reachability/ball prediction/physics. Each field must be a value, and
`Observation` must never grant a write path back into runtime objects.

## 7. Known protocol gaps

1. **Single action vs ordered candidates.** Legacy `_AddPanicPass`,
   `_BallControlCommand`, `_TrapCommand`, `_InterfereCommand` and
   `_SlidingCommand` push several ordered `PlayerCommand`s; `PlayerControl`
   carries one `ControlAction`. P21.4 keeps the single best intent plus the
   existing movement fallback. If testing shows a material loss, a bounded
   public action-candidate protocol is a separate, separately-designed stage.
2. **Action feedback.** `Player::Play()`'s `RequestOutcome` is not the same
   entry as `PlayerControl → BuildPlayerCommands()`, so
   `LastActionRequestOutcome()` is not a general ACK. P21 does not add
   `request_id`/ACK; it freezes the current "intent may be rejected, deferred
   or unmatched" semantics and tests them.
3. **RNG.** The legacy Eliza uses `Match::rng()` for set pieces, shots and
   panic. The new AI must not introduce RNG into the public policy:
   determinism is an acceptance criterion. Any legacy randomness is either
   dropped or moved to a deliberate, seeded choice, documented per call site.

## 8. Licensing / attribution

The Eliza sources are public domain (Bastiaan Konings Schuiling, 2008-2015)
and carry the Google LLC Apache-2.0 header. Any ported algorithm body keeps
its original header comment and attribution.

## 9. Batches

| Batch | Scope | Acceptance |
| --- | --- | --- |
| P21.1 | `IPlayerPolicy`, `AIEngine`, Default adapter | field-identical default output, digest unchanged |
| P21.2 | minimal Observation extensions | coordinates consistent, no tick/RNG/physics change |
| P21.3 | Eliza off-ball | determinism, no NaN, shape sanity |
| P21.4 | Eliza on-ball actions | safe fallback, arbitration still applies |
| P21.5 | keeper + tactics | Coach/Player split preserved |
| P21.6 | frequency + request semantics | current semantics frozen and tested |
| P21.7 | policy switching + full regression | Default and Eliza drive 22 players |
