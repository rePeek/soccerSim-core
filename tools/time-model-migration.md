# Simulation time migration

Unit migration and football-clock/restart policy changes are separate commits.
The target is a fixed 100 Hz simulation grid, with absolute `Tick` and duration
`TickSpan` values. Integration remains float seconds derived from that grid.
No configurable physics dt, wall-clock pacing, or foundation time module.

## Completed stages

- **T0:** `sim/tick.hpp` defines strong time values, checked arithmetic,
  `Seconds`/`Minutes`, and `kTickSeconds`. `tick_boundary.hpp` contains exact
  millisecond conversion, rejecting off-grid input rather than rounding it.
  These STL-only headers are sim value contracts, not actor-runtime exports.
- **T1:** `Match::now_` is the only timeline storage, in ticks.
  `AdvanceTime(TickSpan)` advances it; normal executed steps advance one tick.
  `BuildWorldState` reads its value directly, without dividing milliseconds.
  The body/ball collision timestamp/cooldown and mental-image capture cadence
  also use ticks. The unused write-only goal timer was deleted, not replaced.
  Existing `GetActualTime_ms`/`BumpActualTime_ms` are temporary caller adapters,
  not a second stored clock. The latter accepts only exact tick-grid durations;
  every existing runtime call is grid-aligned. Arbitrary fractional-tick manual
  timeline bumps are no longer permitted.
- **T2:** Referee stop/prepare/start/foul timestamps and post-restart relaxation
  are tick values. Event intervals are local to Referee. Match owns preparation
  fast-forwarding to a deadline minus the existing simulated tail; Referee no
  longer contains the 1900 ms skip. Preparation/whistle accept crossed deadlines
  and are one-shot via prepared taker/play state. Normal old execution and digest
  encodings are retained; crossed-deadline recovery has dedicated regressions.
- **T3a:** Player action elapsed/duration/optional contact use `TickSpan`, with
  animation indices projected from the sole cursor. Completion/contact crossings
  are one-shot; large advances saturate before addition. Decision and locomotion
  refresh schedulers use strong ticks and context-dependent local cadences.
  Tests cover crossed cadences, changed context, zero-contact entry, resets and
  overflow. Regression, identity and autonomous app outputs remain identical.
- **T3b:** Player last-touch/card-effect storage and Referee/Team writers use
  `Tick`; history/tactical refresh cadences use owner-local ticks. Removed the
  unused possession-duration accumulator/API. Touch-time readers in Humanoid
  remain exact temporary projections pending its calculation migration.
  Card-effect regression covers both processing orders and the exact due tick.
- **T4a:** Ball prediction generation iterates TickSpan and uses `kTickSeconds`.
  Ball owns horizon/cache policy in `ball_timing.hpp`; native Ball/MentalImage
  horizon APIs accept TickSpan, and MentalImage stores only a Tick capture time.
  Sampling adapters temporarily retain previous/negative-horizon behavior while
  calculation callers migrate. Saturating sample indices are overflow-safe.
  Removed unused Ball/Player history-mean readers and their write-only histories.
  Extrapolation/rotation arithmetic order and quaternion rate encoding remain
  unchanged; this stage is not a rotation physics/model rewrite.

These stages leave the legacy football clock, scale, fatigue, action progression,
normal restart schedule and RNG windows unchanged. They do **not** yet implement the
end-state rule that all simulation APIs/state use ticks. Football time can still
have sub-tick increments; its millisecond storage is intentionally transitional.
The temporary conversion in `AdvanceTime` preserves the exact previous float
expression order for that clock and the possession accumulator.

T1 tests cover both processing orders, direct clock advances without physics/RNG,
restart jumps versus executed steps, the normal preparation tail after a jump,
terminal freeze/reinitialization, and atomic rejection of invalid durations.
Existing lifecycle tests still cover a 1 ms half and 1 ms scaled clock progress.

## Remaining stages and semantic boundaries

1. **Referee restart model (semantic, after unit migration):** replace the temporary
   fixed event schedule with Pending/Ready/Taken state and readiness conditions.
   A per-restart policy supplies minimum elapsed time and a maximum-time fallback,
   not a fixed preparation delay. Separate positioning/ball placement, taker and
   legal-opponent readiness, permission to execute, and actual scheduled contact.
   Referee is the first time-policy owner to be rebuilt.
2. **Remaining Player/Humanoid calculations:** migrate remaining time readers
   and grid-based query horizons. Diagnostic output may project old wire units;
   gameplay must not retain duplicate clocks. Action cursor/scheduler/touch/card
   storage is migrated. Unused possession duration and mean histories are removed.
3. **Perception/reachability/prediction:** change grid-based history/prediction APIs
   to tick horizons. Inventory off-grid callers first. Continuous kinematic
   arrival estimates are not deadlines; their precision and ordering must not be
   silently lost by forcing them into integer `TickSpan`. Any rounding policy
   needs explicit tests and a separate behavioral decision.
4. **Clock policy:** introduce timeline/regulation/ball-in-play clocks and explicit
   half-running state. Include ordinary dead balls in regulation, exclude the
   half-time interval, and define added-time/half-time phases separately. Remove
   the old duration scale/fatigue compensation in this semantic stage, not as an
   allegedly digest-preserving rename. Boundary inputs must have an explicit
   policy for non-grid half durations.
5. **Restart scheduling/calibration:** Match owns fast-forward or simulated waiting;
   Referee supplies conditions and earliest/timeout boundaries. Never skip an
   interval in which actor readiness must evolve. Calibrate readiness/minimum/
   timeout policies separately, then measure multiple seeds (effective time,
   cleaned distance, pass contacts, shot contacts and goals, by half).

Time representation does not explain why a duration exists. Keep three categories
separate: the immutable tick quantum; owner-local football/behavior policy; and
legacy scheduling tricks, which are removed rather than renamed. The restart
model must avoid a circular readiness test: current `PrepareSetPiece` places the
ball and teleports actors. Those effects must be separated before readiness can
be evaluated as a precondition. The 300-tick throw-in minimum suggested during
design is illustrative, not a selected production calibration.

### Why three proposed changes cannot be unit-only

- `query/reachability.cpp` returns off-grid integer-millisecond estimates,
  including the close-range tie-breaking proxy. `Match`/`Team`/`Player` compare
  these to select possession candidates. Tick truncation creates new ties and
  can change decisions/RNG trajectories. Reaction delays also need a caller audit.
- `match_duration=49.75` gives a legacy factor of 10 and **1 ms** of football time
  per physical tick; the test explicitly asserts this. Deleting the scale also
  changes the default match's executed duration and distance-weighted fatigue.
  Pure integer regulation ticks cannot preserve all old configurations.
- A preparation deadline 200 ticks away currently skips 190 ticks, then performs
  the remaining normal ticks before preparation. Jumping straight to the deadline
  removes executed steps, including action/physics/RNG opportunities, and changes
  `MatchResult::duration_ticks`. T1 locks this old schedule down so its later
  replacement is an intentional, bisectable semantic change.

There is currently **no** replay, GNN, Coach or analytics scheduler implementation
in `src`. Do not create unused infrastructure for them during this migration.
If 24 Hz replay is later implemented, use integer phase accumulation on the
100 Hz grid, not a four-tick/25 Hz approximation.
