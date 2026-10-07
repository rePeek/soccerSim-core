# Read-only native restart measurements

`football_restart_metrics [seed [half_ticks [reverse_0_or_1 [symmetric_0_or_1]]]]`
composes explicit DefaultAI + Simulation from the app fixtures. Defaults: seed 42,
two 45-minute halves, normal processing, original asymmetric fixtures/difficulty.
The optional symmetric input copies the complete home roster/formation/tactics to
away, preserving distinct away IDs, with both declared difficulties 1.0. No runtime
interposition, new RNG draw, actor placement or fast-forward is added. This is a
diagnostic program, not an analytics scheduler or runtime history API.

```sh
nix develop --command cmake --build --preset release -j 4
for seed in 42 43 44; do
  build/release/football_restart_metrics "$seed" > "/tmp/football-metrics-$seed.txt" 2>&1
done
# Short native diagnostic, including a period-censored restart:
build/release/football_restart_metrics 42 500 1
```

Keep native BqLog output. CSV records have distinctive first fields and can be
selected for analysis without changing/filtering the application's diagnostics.
The probe fails on inconsistent clock increments, per-half totals, call/timeline
counts, or disagreement between independently recorded ordinary-event waits and
regulation minus ball-in-play time.

## Definitions / CSV fields

All timestamps, durations and quantiles are **ticks** (100/s), not milliseconds.
Modes: 1 kickoff, 2 goal kick, 3 free kick, 4 corner, 5 throw-in, 6 penalty.

- `restart,seed,reverse,half,ceremony,mode,entered,authorized,contact,end,timeout,censored`
  records one rule event. Contact is the accepted taker's native touch Tick, not
  the whistle, action expiry, animation selection or a guessed ball-release time.
  Missing authorization/contact is `NA`; `end` is contact or the period whistle.
- Ordinary **preparation** = authorization minus entry (Pending); **ready wait**
  = contact minus authorization; **total** = contact minus entry. This includes
  every simulated positioning tick, not just fixed minimum delays.
- Ceremonies are separately marked: their preparation includes the scheduled
  whistle interval, not ordinary Pending readiness. They do not enter regulation.
- `timeout` means actual one-shot legal-placement repair, not reaching a nominal
  timeout value. It does not imply fabricated contact.
- Period-abandoned events are **censored**. Their observed wait contributes to
  dead-ball accounting, but is excluded from completed-wait quantiles/means.
- `half,seed,reverse,half,regulation,effective,dead` uses actual clock differences;
  dead = regulation - effective. It excludes the opening/half-time ceremonies.
- `distribution,seed,reverse,half,ceremony,mode,count,censored,timeouts,...`
  then contains three groups `name,n,mean,p50,p90,max` for prepare/ready/total.
  Quantiles are lower empirical order statistics (indices floor((n-1)*p)); no
  interpolation. Empty groups use `NA`. Means are display-only floating values.
- `match,seed,reverse,home_score,away_score,calls,timeline,regulation,effective`.

This does **not** count completed passes, official shots-on-target or saves.
Those require separate event definitions; touches are not interchangeable metrics.

## Initial full-match evidence after S2

Production semantics: `16be5f4`. Normal order, seeds 42/43/44, unmodified default
fixtures/difficulty (home 1.0, away 0.6). These are **not symmetric-team calibration**.
Every run has 540000 regulation ticks and 541076 executed calls. Seed 42 matches
the autonomous GameEnv app's 27–26 result, confirming no diagnostic intervention.

| Seed | Score | Ordinary restarts | Effective, half 1 | Effective, half 2 | Effective, total |
|---:|---:|---:|---:|---:|---:|
| 42 | 27–26 | 148 | 39:04.14 | 37:41.43 | 76:45.57 |
| 43 | 29–11 | 131 | 39:42.68 | 38:53.90 | 78:36.58 |
| 44 | 29–16 | 135 | 39:00.41 | 39:09.67 | 78:10.08 |

The two half-opening ceremonies are additional, separately classified events.
No ordinary event is period-censored or uses timeout repair in these full runs.
Native short runs exercise censoring in both processing orders.

Completed ordinary waits: **count / mean total seconds** (not policy minima):

| Mode | Seed 42 | Seed 43 | Seed 44 |
|---|---:|---:|---:|
| Post-goal kickoff | 53 / 8.60 | 40 / 8.52 | 45 / 8.66 |
| Goal kick | 53 / 3.83 | 56 / 3.81 | 51 / 3.83 |
| Free kick | 5 / 2.94 | 2 / 3.08 | 2 / 2.88 |
| Corner | 4 / 6.23 | 6 / 6.03 | 5 / 5.99 |
| Throw-in | 33 / 2.91 | 27 / 3.21 | 32 / 2.80 |
| Penalty | 0 / NA | 0 / NA | 0 / NA |

Independent sums of ordinary event waits exactly equal dead-ball clocks in each
half: seed 42 **35586/43857**, 43 **31732/36610**, 44 **35959/35033** ticks.
This checks definitions/accounting, not football realism. Opening ceremony waits
are 633 ticks; second-half waits are 442 ticks in these three full runs.

## Interpretation / next work

- Contact authorization is not the main delay: mean Ready waits range roughly
  0.25–0.79 s. Pending positioning/legal readiness supplies most recorded waits.
- Goal-kick/throw-in waits are short; current setup places the ball immediately
  and has no retrieval model. Lengthening a fixed minimum to hit an exact effective
  duration would hide missing behavior rather than calibrate it.
- Scores are still extremely large and outcomes seed-dependent. Time representation
  and restart readiness alone do not repair shot selection, coordinated defence
  or goalkeeper timing. Restart frequency is coupled to those behaviors.
- Three normal-order default-team samples establish a reproducible starting point,
  not a validated distribution. Extend to more seeds, symmetric teams, both orders,
  event-local geometry, cleaned distance and correctly defined action contacts.
  Compare comparable real-match distributions before changing owner-local policy.
- Do not prescribe exactly 56:58 (or any other fixed effective time) per match.
  All restart bounds, physics/animation rates and AI coefficients remain unchanged
  in this diagnostic stage. No balance or realism calibration is claimed.

## Verification

- Release: complete 29/29 CTest registrations and core-only build pass.
- Debug/NDEBUG: all 28 non-default registrations pass; the final two 500-tick
  censoring diagnostics also pass with identical CSV across all three modes.
  S2 full default matches already passed in both modes; no production code changes
  in this measurement stage.
- Each full seed was repeated: raw outputs are byte-identical. Per-half ordinary
  wait sums exactly reconcile with the independently accumulated dead-ball clocks.
- Native diagnostic output is retained and baked assets remain unchanged.

## Symmetric/order matrix: pre-processing-frame correction

Measured at runtime `85e4bdf`, without production changes. The optional symmetric
input is declared before Init; it changes diagnostic match inputs, not runtime
coefficients or restart timing policy. Only symmetric runs print an additional
`setup,seed,reverse,symmetric_home_copy,home_difficulty,away_difficulty` descriptor;
original-input records remain unchanged. Do not pool runs from different setups.

```sh
for seed in 42 43 44; do
  for reverse in 0 1; do
    build/release/football_restart_metrics "$seed" 270000 "$reverse" 1 \
      > "/tmp/football-matrix-symmetric-$seed-$reverse.txt" 2>&1
  done
  build/release/football_restart_metrics "$seed" 270000 1 \
    > "/tmp/football-matrix-default-$seed-1.txt" 2>&1
done
```

| Symmetric seed | Order | Score | Ordinary events | Effective | Censored | Timeout |
|---:|---|---:|---:|---:|---:|---:|
| 42 | Normal | 17–21 | 126 | 79:14.25 | 1 | 0 |
| 43 | Normal | 29–13 | 140 | 78:03.32 | 1 | 0 |
| 44 | Normal | 18–19 | 142 | 78:07.17 | 1 | 0 |
| 42 | Reverse | 0–0 | 2 | 89:55.18 | 0 | 0 |
| 43 | Reverse | 0–0 | 2 | 89:55.18 | 0 | 0 |
| 44 | Reverse | 0–0 | 2 | 89:55.05 | 0 | 0 |

All three original-fixture reverse runs also finish 0–0 with two ordinary throw-ins
and the same effective times. Every half still has 270000 regulation ticks, and every
event-wait sum reconciles. This is **an execution-frame anomaly, not a realism or
restart calibration distribution**. A read-only 10000-step native probe finds actors
0.77/0.87 m from the observed ball yet reporting no possession and 3000 ms reachability.
The first actor-processing mirror flips the ball away from its first-roster frame
under reverse processing. Observation projection alone cannot repair execution.

All six symmetric full runs repeat byte-identically. The default normal seed42 raw
output matches T4e byte-for-byte. Release/Debug/NDEBUG each pass 31/31 registrations,
including new short symmetric/order cases; that coverage did not detect this long-
run semantic problem. The execution frame must be corrected and causally tested
before using reverse samples for calibration. Bounds/assets/AI remain unchanged.
