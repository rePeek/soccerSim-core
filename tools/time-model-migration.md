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

The unit-only stages T0–T4a left the then-legacy football clock, scale, fatigue,
action progression, restart schedule and RNG windows unchanged. They did not
implement the end-state rule that all simulation APIs/state use ticks. At that
stage, temporary AdvanceTime conversion preserved scaled sub-tick clock progress
and float expression order. S2 deliberately retires that compatibility policy.

Current timeline tests cover both processing orders, direct advances without
physics/RNG, executed pending positioning, minimum/timeout crossing, terminal
freeze/reinitialization and atomic rejection of invalid durations. The former
preparation-tail assertions were deliberately replaced during S1, not during T1.
S2 retires 1 ms halves/scaled increments: the minimum native half is one tick.

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

## S2: three clocks and unscaled football time (semantic change)

Match now owns a Tick timeline and TickSpan regulation/ball-in-play accumulators.
Each half begins on its actual accepted kickoff. Ordinary dead balls continue
regulation but not ball-in-play. EndHalf stops both through the ceremony. The
old StartPlay/in_play flag authorizes execution; it is not an effective-time fact.
Both ceremonial kickoffs retain their placement/deadlines but now enter Ready
and need actual scheduled taker contact to become Taken. Only the authorized
taker can execute contacts before the ball is live; body collisions/goals require
actual live play. Empty controls cannot take a kickoff or advance either clock.

MatchOptions::half_duration is native TickSpan (Minutes(45) by default). CLI
--half-duration-ms is a positive exact 10 ms-grid boundary. Native validation
rejects zero/full-match overflow before RNG draws, without millisecond capacity
limits. AdvanceTime checks before publication, clips football clocks at the current
period, derives possession seconds from admitted ticks and freezes after full time.
The referee whistles independently of Pending/Ready/Taken at the period boundary.

Removed match_duration, matchDurationFactor, authoritative match milliseconds and
the inverse-scale fatigue compensation. Fatigue uses real distance during an
underway half, including dead-ball positioning; ceremonies do not consume it.
Physical integration, SI formulas, animation rates, reachability precision and
restart min/max bounds are unchanged. This does not establish realism.

All pre-stage fingerprints and causal evidence are archived in
test/baselines/pre_three_clocks.md. World digest fields necessarily change; the
unchanged motion/action/RNG encoding exposes intentional trajectory changes.
Request/identity fixtures now wait for real contact rather than elapsed whistles.

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
4. **Future period policy:** added time, extra time, shootouts and explicit richer
   half-time ceremonies are separate football-rule decisions; S2 implements only
   two bounded regulation halves, not full IFAB timing.
5. **Multi-seed restart measurement/calibration:** count restarts by mode/half,
   Pending/Ready/taken waits and effective-time distributions before tuning bounds.
   Also measure cleaned distance, pass contacts, shot contacts and goals by half.
   Never prescribe a single fixed effective duration or skip evolving readiness.

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
- Historically match_duration=49.75 gave a factor of 10 and 1 ms of football
  time per physical tick. S2 intentionally retires that test/configuration and
  changes default duration/distance-weighted fatigue. Pure integer regulation
  ticks cannot preserve every old scaled configuration.
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

## S2 verification

- Release, Debug and true NDEBUG: 27/27 CTest registrations each, including full
  90-minute default matches. Short/full results and all core/identity rows agree.
- Core-only build passes. Native NDEBUG flags and unpatched BqLog are retained.
- Default: 27–26 / home_win / 541076 calls; 180-tick halves: 0–0 / 1436 calls.
  These are policy-stage results, not realism calibration or unit-only equality.
- Lifecycle tests cover both orders, actual kickoff, dead-ball/half-time boundaries,
  overflow atomicity, precision and terminal freeze. Native movement tests verify
  exact unscaled per-metre fatigue and exclusion of ceremonial warmup.
- Baked SHA256 remains the S1 value; all old rows are archived before replacement.

## Post-S2 restart measurement (no policy change)

tools/football_restart_metrics.cpp adds read-only event/clock diagnostics, not
runtime instrumentation or a scheduler. Native short runs cover both orders and
period-censored waits; completed-wait distributions exclude censoring. Independent
ordinary-event wait sums must equal regulation minus ball-in-play ticks per half.

Initial full normal-order seeds 42/43/44 produce 148/131/135 ordinary restarts and
76:45.57 / 78:36.58 / 78:10.08 effective time. None use timeout repair. Default
teams are asymmetric; this is measurement, not symmetric-team realism calibration.
Definitions, by-half/mode results and rerun commands: tools/restart-metrics.md.
All restart bounds, AI/physics coefficients and animation timing remain unchanged.
Remaining work is richer restart behavior and distributional calibration, alongside
the previously listed continuous reachability/Player calculation audits.
