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
