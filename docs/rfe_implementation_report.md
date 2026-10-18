# RFE Implementation Report

Base Commit: `819f087` (clean worktree; no RFE/TEAM-1 implementation).
Final Commit: see the phase commits in `git log` (this report travels with them).

## Scope

RFE-1A through RFE-1D only. Restart planning, placement, release rules,
RefereeBuffer authority, Team and Coach integration are deliberately deferred.
The actual source paths are `src/runtime/` and `src/referee/`, not `src/sim/`.

## RFE-1A — synchronous commit boundaries

- Submit appends only. `CommitPendingRulings` invokes the exhaustive
  `CommitRuling` visitor in submission order and drains the queue once.
- Commit after Advance, out-of-play evaluation, pending-foul evaluation and
  accepted-touch consumption; retain the goal commit after frame restoration.
- The end-of-step commit is unconditional, not gated on ball-in-play.
- Tick entry drains rather than discards pending rulings. Situation reset no
  longer discards verdicts; match initialization remains a queue reset boundary.
- The test-only rule driver uses the same synchronous boundaries.
- Short runtime tests cover deferred submission, repeated drain, front/touch
  boundaries and verdict survival across situation reset. No full match run.

## Remaining legacy dependencies

Referee still owns RestartState/RestartPlan and manipulates restart actors/ball
through the existing compatibility execution path. No parallel controller or
second writable restart authority is introduced. Foul/offside state still borrows
Player pointers. Event cursors, richer rule events and Coach wiring are RFE-5/6.

RFE-1A validation: Debug build and CTest 11/11 passed (175.53 s). Existing
bounded deterministic runtime acceptance is unchanged. No rule, movement or
RNG algorithm changed in this infrastructure phase.

## RFE-1B — stop verdict producers

- Foul, offside, touchline and goal-line settlement explicitly receive a
  RulingSink and submit StopPlayRuling, never invoke the runtime stop port.
- A stop does not execute until the enclosing rule operation returns. The
  existing buffer gate prevents a later same-tick touch from awarding again.
- Controlled runtime tests use frozen foul evidence and an actual accepted
  offside touch, without waiting for opening ceremonies. They verify deferred
  mutation, immediate action restriction, duplicate-check suppression and out
  of play. No restart planning/placement or RNG arithmetic was changed.
- RuleCommandSink::StopPlay remains unused compatibility API until RFE-7.

RFE-1B validation: 8 ruling cases / 25 assertions passed; Debug build and
CTest 11/11 passed (169.08 s).

## RFE-1C — restart award producers

- ScheduleRestart submits the confirmed kind/team/home-frame position for
  fouls (including penalties), offside, throw-ins, corners, goal kicks and
  post-goal kickoffs. Opening/half-time Advance submits the ceremonial award
  once after its legacy preparation. Runtime alone appends RestartAwardedEvent.
- AwardRestart does NOT implicitly stop play in this compatibility phase:
  ordinary producers submit Stop then Award in the same synchronous drain;
  ceremonial kickoffs are already stopped. No second executor is introduced.
- Restarts still execute from RefereeBuffer, explicitly temporary until RFE-2.
  No change to PlanRestart, preparation timings, legal release or touch kernels.
- Found and repaired a missing goal-kick `else` in SettleGoalLine: attacker
  last touch over the opposite goal line previously reused the previous kind,
  team and spot. The new award is GoalKick to the defending team at the centre
  of its goal-area front (legacy 5.5 m depth). This is a rule behavior change,
  not an ownership-only refactor.
- Controlled tests cover each existing restart kind, penalty vs free kick,
  goal-line priority over touchline, goal priority and one-time ceremonial
  awards. Awarded never claims ball-in-play.

RFE-1C validation: 12 ruling cases / 65 assertions passed; Debug build and
CTest 11/11 passed (169.07 s). A17 remains `5bef607ddd567a09` (22 actors,
18 switches), as in RFE-1B. The goal-kick correction is outside that tape;
its controlled first rule-state difference is Tick 100: old stale KickOff /
Home / centre vs GoalKick / Away / (47,0,0). Settlement draws no RNG;
subsequent reset/planning and actor/ball trajectories intentionally differ.
No complete-match claim is made.

## RFE-1D — disciplinary commit and final boundary hardening

- CheckFoul submits CardRuling (PlayerId, legacy type 2/3, now + 600 ticks);
  it no longer calls GiveYellowCard/GiveRedCard. Runtime increments the same
  Player count immediately and preserves the old delayed execution deadline.
  The 1000-tick card-administration restart delay is unchanged.
- CardShownEvent now distinguishes a second-yellow dismissal from a direct
  red (`red=true`, `second_yellow=true` for the former). No extra +3 count is
  added for a second yellow: the original +1/+1 send-off rule remains intact.
- All pending verdicts from a completed rule operation form an ordered logical
  batch. Validate every payload before any executor/event runs. Invalid team,
  unsupported mode, nonfinite spot, unknown/inactive card recipient, invalid
  card type or past new card deadline throws logic_error, retaining the failed
  queue for diagnosis. No silent card drops or partial validation commits.
- Exact card decisions (player/type/effective deadline) have match-scoped
  receipts, preventing duplicate counting/events in one drain and across drains.
  This key is sufficient for the current single-pending-foul producer. A future
  multi-decision producer should introduce explicit decision IDs. Init/Stop
  clear both the pending queue and receipts; situation reset does not.
- A new real one-step goal test exposed a boundary trap in the intermediate
  RFE-1A change: committing a goal stops ball-in-play, so a subsequent live-ball
  guard could skip scorer/event publication. Goal score, scorer and formal
  event now all belong to ApplyGoalRuling, after frame restoration at the same
  original tick. Duplicate reports are ignored until the ensuing reset.
  This restores the baseline goal path and also makes diagnostic commits
  complete. The original last-touch/own-goal arithmetic is preserved.
- Focused tests cover actual yellow/red foul producers, second yellow, exact
  599/600-tick delayed dismissal, ordinary yellow staying active, event order,
  card-administration deadlines, replay/Stop/Init, seven invalid-batch cases,
  real physical goal + next-step kickoff, and standing-foul advantage recall/
  expiry. No long/full-match run, no new physics or animation algorithm.

## Delivery summary

Completed Phases: RFE-1A, RFE-1B, RFE-1C, RFE-1D.

Changed Files:
- `src/referee/{referee.hpp,referee.cpp,ruling.hpp}`
- `src/runtime/{simulation.hpp,simulation.cpp}`
- `src/event/match_event.hpp`
- `src/player/player.hpp` (read-only disciplinary accessors only)
- `src/testing/simulation_access.hpp`
- `unittests/runtime/{CMakeLists.txt,referee_ruling_test.cpp}`
- `docs/rfe_implementation_report.md`, `AGENT.md`

Ruling Producers: GoalMouthCrossed -> Goal; foul/offside/line settlement ->
Stop; ScheduleRestart + ceremonial Advance -> AwardRestart; CheckFoul -> Card.

Runtime Executors: CommitPendingRulings validates the ordered logical batch;
CommitRuling dispatches exhaustively to StopPlay and ApplyGoal/Restart/CardRuling.

Removed Direct Mutations: all referee StopPlay calls and both direct card
writes. Restart ResetPosition/SelectRetainAnim and ball/reset/start ports are
intentionally NOT removed in this phase.

Restart Ownership: still solely RefereeBuffer/RestartState and its old executor;
Runtime confirms the award and publishes the event, not a parallel restart plan.

Team Integration: none (TEAM-1 absent at baseline; RFE-4 deferred).
MatchEvent Producers: Runtime confirms GoalScored, RestartAwarded, CardShown;
existing EventRecognizer pass/shot producers unchanged. Richer events deferred.
Coach Integration: none; existing AI/Coach code and physics sequencing unchanged.

First Behavior Difference: additive formal awards can appear at opening Tick 0;
the documented rule-state change is the missing goal-kick repair at controlled
Tick 100. No new RNG draws. Native A17 equivalence does not prove all possible
goal-kick trajectories remain identical (they intentionally cannot).

Remaining Legacy Dependencies / Deferred Work: RefereeBuffer cross-module
reads; pointer-based Foul/offside/RestartPlan; referee-owned placement/release;
RuleCommandSink restart execution and its unused StopPlay compatibility method;
Runtime RestartContext/PlayState, Team integration, extended restart legality,
event cursors, richer events, Coach consumption and legacy cleanup (RFE-2–7).

Known Risks: the legacy ready/taken speed/action tests remain compatibility
logic, NOT complete IFAB legality; this change adds no fake touches or timer
release. Logical batch validation is not a resumable simulation transaction on
allocation/runtime exceptions; callers must not resume an unsuccessful Step.
Legacy referee preparation is still outside the new executor until RFE-2.

Tests: RFE-1D Debug build passed; `[ruling]` passed 17 cases / 186 assertions;
`ctest --preset debug` passed 11/11 (173.70 s). All four phase builds/CTest
runs passed independently. Final A17: 22 actors / 18 switches / digest
`5bef607ddd567a09`, unchanged from the baseline. `git diff --check` passed.
No asset, Snapshot structure, physics kernel or RNG algorithm was modified.

Phase commits:
- RFE-1A: `5524c9b` — refactor: unify synchronous ruling commit boundaries
- RFE-1B: `f5a3e2c` — refactor: route play stops through referee rulings
- RFE-1C: `9e7c85a` — refactor: publish restart awards from referee
- RFE-1D: this report's commit — refactor: route disciplinary decisions through rulings

Final Commit is the RFE-1D commit above; obtain its exact hash with
`git log -1 --format=%H --grep='route disciplinary decisions through rulings'`.
No push was performed.

## RFE-2A/2B — Runtime owns restart execution (single commit)

The ownership migration and the cross-module reader migration are one commit
on purpose: the acceptance gate for RFE-2 is that no second writable restart
authority survives, so an intermediate `Referee::GetBuffer()` adapter would
have recreated exactly the dual authority the plan forbids.

- `RestartContext`/`RestartState`/`RestartPlan`/`RestartPhase` now live in
  `src/runtime/restart_context.hpp`; the planner/placement modules moved to
  `src/runtime/restart_planner.*` and `src/runtime/restart_placement.*`.
- `RestartController` (`src/runtime/restart_controller.*`) is the single
  executor. It owns scheduling (`PolicyFor`), the opening/half-time ceremony,
  setup planning, timeout placement, readiness polling, legal release and the
  post-restart line grace. It mutates actors/ball only through the composition
  command port and borrows the same immutable `RefereeTickFacts` the rules use.
- The Referee retired its whole `RefereeBuffer` and `post_restart_relax_`. It
  keeps only `Foul` and the offside-reference list. It submits
  `AwardRestartRuling` (now carrying the administration window) for every
  restart and a new `BallInPlayRuling` when it confirms a legal release; it no
  longer calls any `RuleCommandSink` lifecycle method.
- Runtime signals rule-state side effects back through
  `Referee::OnCeremonialSetup()` (clears the stale offside reference) and
  `Referee::OnRestartTaken()` (abandons the producing foul), so the rule state
  still changes at the exact legacy instant.
- `PlayerTickContext`/`PlayerCommandInputs` and `WorldStateBuilder` now borrow
  `Simulation::restart_context()`; `restart_needs_simulation`, `restart_taker`,
  `restart_target` and the per-player target lookup (`RestartTargetFor`) read
  the Runtime context. `RefereeBuffer`, `GetBuffer`, `RestartNeedsSimulation`
  and `GetRestartTarget` no longer exist anywhere.

Behavior preservation: restart scheduling, ceremony placement, RNG reseed
points, plan/readiness algorithm, timeout placement, release conditions and
the grace countdown are equivalent. The only intentional interface addition is
the administration field on the award ruling, which carries the pre-existing
card-administration delay without duplicating the policy. The opening ceremony
still publishes exactly one `RestartAwardedEvent` on its placement instant and
never claims the ball is in play.

RFE-2 validation: Debug build passed; `[ruling]` 20 cases / 214 assertions;
`ctest --preset debug` 11/11 (293.97 s). New tests assert the Runtime context is
the same object players and `WorldState` read, that the opening ceremony state
is Runtime-owned, and that no referee buffer remains.

## RFE-3 — stopped play keeps a running 100 Hz tick

- `src/runtime/play_state.hpp` adds a *derived* `PlayState`
  (`Playing`/`Stopped`/`RestartSetup`/`RestartReady`). `Simulation::
  CurrentPlayState()` classifies the existing authorization flags plus the
  restart context; it is not a second writable authority.
- The `StepImpl` early-return condition was named and made explicit as
  `Simulation::ShouldSkipPlayerSimulation()`. It is byte-for-byte the legacy
  predicate: a stopped instant skips player/ball simulation only when no restart
  is being organized and the instant is outside the opening/half-time placement
  window. Inside a pending restart the tick still runs actors, animation,
  possession and clock, so the heuristic AI walks the sides to their Runtime
  targets and readiness is polled from real state. The opening/half-time
  ceremony early-return path is retained, never deleted unconditionally.
- A confirmed legal release (`BallInPlayRuling`) is the only path that starts
  the ball-in-play clock; the context completes on the following instant.

RFE-3 validation: a bounded `[ruling]` case asserts the
`Playing -> RestartSetup -> RestartReady -> Playing` derivation, drives the
documented timeout fallback (which repairs positions without inventing a
touch), and confirms the Runtime release/complete transition. Full
`ctest --preset debug` remains 11/11.

## RFE-2/3 delivery summary

Completed Phases: RFE-2A, RFE-2B, RFE-3.

Changed Files:
- new: `src/runtime/{restart_context.hpp,restart_controller.*,play_state.hpp}`
- moved: `src/referee/restart_readiness.* -> src/runtime/restart_planner.*`,
  `src/referee/restart_placement.* -> src/runtime/restart_placement.*`
- `src/referee/{referee.hpp,referee.cpp,referee_state.hpp,referee_view.hpp,
  ball_touch_facts.hpp,action_authorization.hpp,ruling.hpp,CMakeLists.txt}`
- `src/runtime/{simulation.hpp,simulation.cpp,simulation_rule_touch.cpp,CMakeLists.txt}`
- `src/player/{player_tick_context.hpp,player_control_builder.*}`
- `src/observation/world_state_builder.*`
- `src/testing/simulation_access.hpp`
- `unittests/runtime/{referee_ruling_test.cpp,simulation_runtime_test.cpp}`
- `docs/rfe_implementation_report.md`, `AGENT.md`

Ruling Producers: foul/offside line settlement -> Stop + AwardRestart (+Card);
accepted legal restart contact -> BallInPlay. Ceremonial kickoff awards are
Runtime procedures that publish `RestartAwardedEvent` directly.

Runtime Executors: `RestartController` owns schedule/setup/placement/timeout/
readiness/release/grace; `Simulation` validates the ordered ruling batch and
executes StopPlay, Goal, Restart, Card and Ball-in-play.

Removed Direct Mutations: the Referee no longer owns a restart buffer, calls
`ResetSituation`/`ResetBall`/`SetBallRetainer`/`StartPlay`/`StartSetPiece`/
`StopSetPiece`/`StartBallInPlay`, or calls `ResetPosition`. All restart actor
and ball writes are Runtime.

Restart Ownership: `Simulation::restarts_` (`RestartController` + one
`RestartContext`) is the only writable restart authority. It is the source of
`PlayerTickContext.restart`, `WorldState.restart/restart_taker/restart_pending`
and `WorldPlayerState.restart_target`.

Team Integration: none (TEAM-1 absent; RFE-4 deferred). Set-piece orders,
captains and formation presets are not read yet; the equivalent planner is the
legacy `PlanRestart`/`PositionRestartPlayers` behavior.

MatchEvent Producers: GoalScored, CardShown, RestartAwarded (referee and
ceremony), PassCompleted, ShotEnded. `BallInPlayRuling` is the execution path,
not an event; `RestartTaken` remains RFE-5.

Coach Integration: none; the AI/Coach code and its WorldState reads are
unchanged (`world.restart*` now comes from Runtime).

First Behavior Difference: none expected in rule or motion semantics. The only
observable interface change is internal. The post-restart grace is sampled
after the restart tick, matching the legacy single-owner ordering. No new RNG
draws.

Remaining Legacy Dependencies / Deferred Work: Team set-piece orders and
formation presets; heuristic side coordination for set pieces; value-typed
planner targets (the plan still holds `Player*`); `RestartTaken`/`Authorized`
events; Coach event consumption; `RuleCommandSink` parameters on the Referee
rule functions are now dead and are removed in RFE-7; Referee `commands` and
unused `home/away` parameters await cleanup.

Known Risks: the legacy ready/taken speed/action tests remain compatibility
logic, not complete IFAB legality. The timeout fallback still repairs positions
directly (documented Runtime executor, no fabricated touch). The value-typed
rule-fact view is not yet introduced, so the Referee still borrows `Player*`
for foul/offside identity.

Tests: `ctest --preset debug` 11/11 (293.97 s). `[ruling]` 20 cases / 214
assertions. No asset, Snapshot structure, physics kernel or RNG algorithm was
modified. `git diff --check` passed. No push was performed.

## RFE-4 — Team set-piece orders and formation presets in the RestartPlanner

The Runtime planner now consumes the static Team configuration TEAM-1 added. The
Referee keeps legality and the heuristic Player AI keeps movement; no actor or
ball mutation moved into the team module.

- `src/runtime/restart_planner.hpp` adds `RestartTeamPlanning` (borrowed
  orders/presets/`ActiveLineupState`) and `RestartPlanningTable`. `PlanRestart`
  takes the table, selects the taker from the declared `SetPieceOrders` priority,
  then seeds non-taker targets from the matching `FormationPresets` entry.
- Taker selection uses `football::team::SelectEligibleTaker` over nearest-first,
  stable roster-ordered active candidates. Runtime supplies live eligibility
  (active actors); an unavailable priority entry falls through to the next
  declared name, then to `SelectionFallback::CandidateOrder` (the legacy nearest
  actor). Mapping: Penalty -> penalties, FreeKick -> direct free kicks (the
  current `e_GameMode` cannot distinguish direct/indirect), Corner ->
  `corners_left`/`corners_right` from the attacking team's own perspective,
  ThrowIn -> throw_ins. KickOff/GoalKick have no category and stay legacy.
- Target seeding: the awarded team uses an `Attacking` preset and its opponent a
  `Defending` preset for the same restart kind. Slots bind to live actors through
  `ActiveLineupState`; anchors convert with `FormationAnchorToWorld` in the home
  frame (`defending_direction = Side(team_id)`, no lateral mirror) and then pass
  through the existing `LegalTarget`/`LegalPosition`/separation grid. Presets are
  therefore tactical preferences, never referee-approved placements.
- `RestartController` and `Simulation::AdvanceRestart` thread the borrowed table
  into the planner. The opening/half-time kickoff ceremony stays the legacy
  Runtime procedure, so declared config does not change it; post-goal and
  ordinary restarts use the planner. The heuristic policy already consumes
  `restart_target`, so no AI code changed.

Empty orders/presets, and any restart kind without a matching preset, take the
exact legacy path. The bounded A17 digest stays `5bef607ddd567a09`.

Tests: `[team]` in `unittests/runtime/referee_ruling_test.cpp` — a declared
free-kick order naming the goalkeeper beats the nearest actor while the empty
order still picks the nearest; attacking and defending presets seed the
respective slot-bound actors' targets while the declared taker keeps the ball
spot. `unittests/runtime/team_ownership_test.cpp` retains the opening-ceremony
equality check (legacy vs configured) under a corrected name.

Validation: Debug CTest 13/13 (337.31 s); runtime 76 cases / 17483 assertions;
A17 digest `5bef607ddd567a09` unchanged. No physics, animation or RNG algorithm
changed. `git diff --check` clean. No push.

## MAIN-1 — autonomous-match integration acceptance

There was no automated acceptance that a complete autonomous match finishes.
`unittests/integration/autonomous_match_test.cpp` adds it as a separate,
Release-only layer that links only `football::simcore` (the product `GameEnv`
composition) and runs to `Finished()`.

- Build gate: `FOOTBALL_INTEGRATION_TESTS` defaults ON for the Release preset
  (`-O2/-O3`) and OFF for Debug, so `ctest --preset debug` stays at 13 tests and
  its existing budget. `ctest --preset release` runs 14 tests including
  `autonomous_match`. Force it anywhere with `-DFOOTBALL_INTEGRATION_TESTS=ON`.
- Duration: a full-activity Step costs O(100 ms) on the reference host, so the
  default half is 4 s (`FOOTBALL_INTEGRATION_HALF_SECONDS` overrides it) and the
  file runs 3 test cases / 4 matches. The default suite took 10m45s locally.
- Scenarios: Default x Default completion, Heuristic x Heuristic completion,
  Team set-piece configuration, and same-seed determinism (per-tick world digest
  plus event-trajectory sequence and final `MatchResult`).
- Health audit, not just `Finished()`: restart episodes exist and are actually
  resolved into play, longest restart run < 60 s, longest in-play near-still and
  frozen runs < 30 s, finite and in-bounds ball/player positions, possession
  exists, and a sane final result. The Team match also asserts that any restart
  carrying the declared order uses the declared taker (`mismatches == 0`); a
  short run that happens to contain no configured restart only warns, and the
  deterministic RFE-4 coverage stays in the `[team]` runtime tests.

Concrete blocker found and fixed: `AIEngine` threw `unknown PlayerPolicyKind`
for `PlayerPolicyKind::Heuristic`, because it builds a legacy `IPlayerPolicy`
and a versioned `IPlayerPolicyV2` from the same kind and the legacy factory
threw for the V2-only kind. `MakePlayerPolicy` now returns `nullptr` for
Heuristic (Update already selects the V2 path), with a regression case in
`unittests/ai/policy_switch_test.cpp`.

Validation: Debug `ctest --preset debug` 13/13 (309.14 s); Release integration
suite 3 cases / 52 assertions passed (10m45s); `ctest --preset release -N`
lists `autonomous_match`. No physics, animation or rule algorithm changed.
