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
- **T3c:** Humanoid animation requeue and Player tactical staggered cadences
  consume ticks directly. A reduced-remainder helper avoids offset overflow;
  exhaustive old/new cadence tests include both roster phases and maximum Tick.
  Humanoid's recent-touch grace reads Tick directly. Numerical/RNG baselines
  are unchanged; this does not replace requeue opportunities with a new policy.
- **T4a:** Ball prediction generation iterates TickSpan and uses `kTickSeconds`.
  Ball owns horizon/cache policy in `ball_timing.hpp`; native Ball/MentalImage
  horizon APIs accept TickSpan, and MentalImage stores only a Tick capture time.
  Sampling adapters temporarily retain previous/negative-horizon behavior while
  calculation callers migrate. Saturating sample indices are overflow-safe.
  Removed unused Ball/Player history-mean readers and their write-only histories.
  Extrapolation/rotation arithmetic order and quaternion rate encoding remain
  unchanged; this stage is not a rotation physics/model rewrite.

The unit-only stages T0–T4a left the legacy football clock, scale, fatigue, action
progression, restart schedule and RNG windows unchanged. They did not implement the
end-state rule that all simulation APIs/state use ticks. Football time can still
have sub-tick increments; its millisecond storage is intentionally transitional.
The temporary conversion in `AdvanceTime` preserves the exact previous float
expression order for that clock and the possession accumulator.

Current timeline tests cover both processing orders, direct advances without
physics/RNG, executed pending positioning, minimum/timeout crossing, terminal
freeze/reinitialization and atomic rejection of invalid durations. The former
preparation-tail assertions were deliberately replaced during S1, not during T1.
Existing lifecycle tests still cover a 1 ms half and 1 ms scaled clock progress.

## S1: ordinary restart semantics (not a unit-only migration)

Ordinary outs, fouls/offside and post-goal kickoffs now use ball-out event →
Pending → Ready → Taken → InPlay. Referee owns entered/earliest/timeout ticks.
Preparation resets ball/action state once at current actor positions, then plans
taker/legal targets without moving actors or consuming RNG. AI receives only
value targets/pending status. Match executes positioning/physics every pending
tick: the 190-tick preparation skip and ten-tick tail API are deleted.

Readiness checks stopped actors near their targets, taker presence, ball position,
height and low movement, opposition separation, kickoff halves, goal-kick area
and penalty keeper/area constraints. A deterministic grid prevents clipped targets
from overlapping. Pending locomotion permits sub-idle speeds: the old idle deadband
otherwise stops the taker about 0.7 m short of a 0.35 m readiness target. Live-play
deadband and the physics primitive remain unchanged. Pending controls cannot touch
the ball. Authorization is separate from an actual accepted scheduled taker touch;
action cursor expiry and repeated throw-in retain anchoring cannot release play.

Referee's ball mirror is not always the first roster's mirror under reverse
processing. The restart-local RefereeBallPitchFrame adapter handles that scope;
plans/observations remain in the fixed home-pitch frame through changes of ends.

Initial engineering minimum/maximum bounds, in ticks: free kick 0/3000, throw-in
200/2000, goal kick 300/3000, corner 500/3000, penalty 200/3000, post-goal kickoff
500/3000. Card administration adds 1000 to both bounds; card effects still occur
after 600. A free kick has no mandatory extra delay when already legally ready.
These are **provisional bounds, not measured realism calibration**. At timeout,
legal ball/actor placement is repaired once; no touch or AI command is fabricated.
Opening and half-time ceremonial kickoffs retain their old schedule/placement via
private PrepareCeremonialKickOff, not the ordinary readiness path.

Core's four numerical/RNG golden pairs and baked assets remain unchanged. Only
the normal-order 3/2-roster identity policy fingerprint changes: its first ordinary
free kick occurs at executed step 207 (entered tick 206). Historical values and
the deliberate policy delta are archived in test/baselines/pre_condition_restarts.md.
The default autonomous result intentionally changes from 2–1 / 31041 steps to
1–2 / 34097 steps; do not confuse this with a unit conversion regression.

## Remaining stages and semantic boundaries

1. **Restart completeness/calibration:** calibrate bounds and actor positioning;
   model richer quick-versus-ceremonial free-kick/wall/card interactions and ball
   retrieval. Opening/half-time ceremonies remain separate. S1 is the bounded
   readiness foundation, not a complete IFAB implementation or a realism result.
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
legacy scheduling tricks, which are removed rather than renamed. S1 avoids
circular readiness: ordinary reset/ball placement does not teleport actors; only
the explicit timeout fallback repairs their positions. The 300-tick throw-in
minimum suggested during design is illustrative, not a calibration requirement.

### Why three proposed changes cannot be unit-only

- `query/reachability.cpp` returns off-grid integer-millisecond estimates,
  including the close-range tie-breaking proxy. `Match`/`Team`/`Player` compare
  these to select possession candidates. Tick truncation creates new ties and
  can change decisions/RNG trajectories. Reaction delays also need a caller audit.
- `match_duration=49.75` gives a legacy factor of 10 and **1 ms** of football time
  per physical tick; the test explicitly asserts this. Deleting the scale also
  changes the default match's executed duration and distance-weighted fatigue.
  Pure integer regulation ticks cannot preserve all old configurations.
- Before S1, a preparation deadline 200 ticks away skipped 190 ticks, then
  performed a ten-tick tail. Altering that schedule changes executed physics,
  action/RNG opportunities and MatchResult::duration_ticks. T1 first preserved
  it; S1 deliberately replaces it with executed readiness waiting in a separate
  semantic commit. No direct-deadline jump masquerades as unit migration.

There is currently **no** replay, GNN, Coach or analytics scheduler implementation
in `src`. Do not create unused infrastructure for them during this migration.
If 24 Hz replay is later implemented, use integer phase accumulation on the
100 Hz grid, not a four-tick/25 Hz approximation.

## S1 verification

- Release, Debug and true NDEBUG: 25/25 CTest executables in each mode.
- Core-only build passes; policy still links without concrete simulation actors.
- `[restart]`: 395578 assertions in seven cases, including 48 pure-plan geometry
  combinations, native ball-out frame checks and 800-step value/RNG replay with
  a sent-off taker replaced under both processing orders.
- Core golden pairs unchanged; all four regression and all four identity rows
  match across the three build modes. Full default result also matches across modes.
- Baked animation SHA256 unchanged:
  `33ab837652da93a795e4886738ca09b92e4b6376c99a2500202572e0a7885b86`.
