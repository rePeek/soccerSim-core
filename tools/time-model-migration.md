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

These stages leave the legacy football clock, scale, fatigue, action progression,
restart scheduling and RNG windows unchanged. They do **not** yet implement the
end-state rule that all simulation APIs/state use ticks. Football time can still
have sub-tick increments; its millisecond storage is intentionally transitional.
The temporary conversion in `AdvanceTime` preserves the exact previous float
expression order for that clock and the possession accumulator.

T1 tests cover both processing orders, direct clock advances without physics/RNG,
restart jumps versus executed steps, the normal preparation tail after a jump,
terminal freeze/reinitialization, and atomic rejection of invalid durations.
Existing lifecycle tests still cover a 1 ms half and 1 ms scaled clock progress.

## Remaining stages and semantic boundaries

1. **Referee deadlines:** move stop/prepare/start/foul/card/relaxation times to
   `Tick`/`TickSpan`, with owner-local timing policy, not a global TimingConfig.
   Add one-shot state for crossed deadlines. Check exact-boundary and jumped-over
   behavior separately. Keep the old executed-step schedule in the unit-only
   commit; new scheduler fast-forward behavior belongs in a policy commit.
2. **Player/action/schedulers:** replace aligned cadences and timestamps; make
   elapsed action ticks the sole frame authority and eliminate duplicate
   millisecond/frame state. Preserve completion/contact crossing and float math.
   Digest serialization must project old wire values while proving equivalence;
   do not regenerate goldens to conceal a unit-only change.
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
5. **Restart scheduling/calibration:** rules schedule owner-local deadlines;
   Match owns fast-forward or simulated waiting. Add realistic restart timings
   separately, then measure multiple seeds (effective time, cleaned distance,
   pass contacts, shot contacts and goals, by half).

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
