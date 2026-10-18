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

RFE-1C validation: 12 ruling cases / 68 assertions passed; Debug build and
CTest 11/11 passed (169.07 s). A17 remains `5bef607ddd567a09` (22 actors,
18 switches), as in RFE-1B. The goal-kick correction is outside that tape;
its controlled first rule-state difference is Tick 100: old stale KickOff /
Home / centre vs GoalKick / Away / (47,0,0). Settlement draws no RNG;
subsequent reset/planning and actor/ball trajectories intentionally differ.
No complete-match claim is made.
