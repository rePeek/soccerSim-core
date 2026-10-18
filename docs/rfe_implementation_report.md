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
