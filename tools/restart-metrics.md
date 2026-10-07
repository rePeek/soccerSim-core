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
do not pool runs from different setups. Behaviour `metrics` rows were added later;
see the definitions section below.

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

## Corrected execution frames (intentional spatial semantics)

The ball is now left in its first-roster frame while the other roster is mirrored
for referee/collisions and first-actor execution. After the shared all-actor turn,
the exit always restores the ball. Possession rollout uses identical scopes.
The special referee reverse compensation helper is removed; the fixed-home adapter
is shared with observation. No AI/restart/physics coefficients changed.
Historical fingerprints and the pre-fix causal failure are archived in
`test/baselines/pre_first_roster_frame.md`. Native reachability now passes at a
nonzero stationary ball for both actors/orders/halves; two 5000-tick halves each
produce accepted open-play kicks, with exact event/clock/score/RNG replay.

Corrected symmetric full matrix (same command/inputs as above):

| Seed | Order | Score | Ordinary events | Effective | Censored | Timeout |
|---:|---|---:|---:|---:|---:|---:|
| 42 | Normal | 17–21 | 126 | 79:14.25 | 1 | 0 |
| 43 | Normal | 29–13 | 140 | 78:03.32 | 1 | 0 |
| 44 | Normal | 18–19 | 142 | 78:07.17 | 1 | 0 |
| 42 | Reverse | 26–12 | 130 | 78:52.30 | 0 | 0 |
| 43 | Reverse | 40–16 | 139 | 77:17.18 | 1 | 0 |
| 44 | Reverse | 31–13 | 134 | 78:08.10 | 0 | 0 |

Corrected original-fixture reverse seeds42/43/44: scores45–13/37–12/43–9,
ordinary138/134/145, effective77:03.54/77:46.34/76:56.44; no timeout/censor.
All event waits reconcile with half dead-time, every match has540000 regulation
ticks. All six symmetric full replays are byte-identical; **all three original
normal full records remain byte-identical to T4e**. Release/Debug/true NDEBUG
pass31/31 each, including the new causal/long replay cases inside the existing
boundary executable; core-only builds, three-mode fingerprints agree, baked asset
hash remains33ab837652da93a795e4886738ca09b92e4b6376c99a2500202572e0a7885b86.

This resolves reverse stagnation, not football realism. Scores are excessive in
both orders and the three-seed reverse home advantage warrants broader behavior
analysis; exact trajectory equality between processing orders is not a contract.
Restart bounds remain provisional. Effective times here must not be forced toward
a desired observed league number by lengthening minima. Shot/pass/save definitions
and independent running/animation evidence remain prerequisites to calibration.
Restart bounds remain provisional. Effective times here must not be forced toward
a desired observed league number by lengthening minima. Shot/pass/save definitions
and independent running/animation evidence remain prerequisites to calibration.

## Behaviour metrics with explicit definitions

Added after the frame correction, in the same read-only program. Output gained one
`metrics` row per played half; `setup`/`restart`/`half`/`distribution`/`match` records
and all internal contract checks are unchanged. Native `Player`/`Match` values are
only read, with no interposition, RNG draw or actor/ball mutation.

```text
metrics,seed,reverse,half,home_open_metres,away_open_metres,
  home_touches,home_shot_contacts,home_pass_contacts,
  away_touches,away_shot_contacts,away_pass_contacts,home_goals,away_goals
```

- `open_metres`: sum of that side's per-tick player displacement while the ball was
  already live at the start of the step. Effective open play only: restart/ceremonial
  positioning and the half-time reflection are excluded, so this is running distance
  during live play, not total locomotion. Team totals, not per-player means.
- `touches`: accepted intentional kicked contacts made during live open play.
  Kickoff/restart ceremonies and accidental contacts are excluded.
- `shot_contacts`/`pass_contacts`: subsets of `touches` classified by the acting
  player's accepted kick action (Shot vs Short/Long/High pass).
- `goals`: score delta attributed to that half. Sums must equal the final score;
  shot/pass contacts must not exceed `touches`. Both are checked before printing.

Metric distinctions remain mandatory: pass contacts are not completed passes (no
reception/target check); shot contacts are not official shots-on-target or goals;
open metres are not per-player load or an animation-rate measure.

### Corrected full corpus (Release, 45-minute halves)

Default fixtures (`reverse 0 = normal processing order`):

| Seed | Order | Score | Open km H/A | Touches H/A | Shot contacts H/A | Pass contacts H/A |
|---:|---|---:|---|---:|---|---|
| 42 | Normal | 27–26 | 128.7 / 129.8 | 813 / 771 | 68 / 48 | 546 / 504 |
| 43 | Normal | 29–11 | 132.3 / 129.8 | 895 / 736 | 77 / 32 | 581 / 517 |
| 44 | Normal | 29–16 | 131.0 / 129.0 | 824 / 763 | 72 / 45 | 565 / 538 |
| 42 | Reverse | 45–13 | 131.9 / 126.6 | 957 / 703 | 102 / 29 | 546 / 506 |
| 43 | Reverse | 37–12 | 130.4 / 125.6 | 864 / 758 | 85 / 47 | 542 / 537 |
| 44 | Reverse | 43–9 | 129.1 / 126.0 | 914 / 732 | 98 / 43 | 553 / 496 |

Symmetric inputs (`symmetric 1`, equal declarations and difficulty):

| Seed | Order | Score | Open km H/A | Touches H/A | Shot contacts H/A | Pass contacts H/A |
|---:|---|---:|---|---:|---|---|
| 42 | Normal | 17–21 | 134.2 / 129.5 | 858 / 815 | 62 / 50 | 593 / 551 |
| 43 | Normal | 29–13 | 132.9 / 127.2 | 862 / 758 | 69 / 42 | 568 / 540 |
| 44 | Normal | 18–19 | 130.0 / 128.0 | 860 / 798 | 76 / 50 | 551 / 549 |
| 42 | Reverse | 26–12 | 133.0 / 126.2 | 854 / 815 | 76 / 53 | 571 / 557 |
| 43 | Reverse | 40–16 | 132.5 / 126.6 | 899 / 764 | 94 / 36 | 562 / 535 |
| 44 | Reverse | 31–13 | 130.7 / 127.3 | 839 / 748 | 76 / 42 | 562 / 539 |

All 12 runs repeat byte-identically and satisfy the goal/contact invariants. Each
side's open-play total is ~126–134 km per match (~11.5–12.2 km per player), in the
same order as real league load. Pass contacts ~500–650 per side are plausible in
count only; shot contacts ~30–100 and goals 9–45 are far above real football, so
finishing/goalkeeping/defensive policy — not the clock — is the dominant source of
excessive scores. These are measurement baselines, not a calibration target: do not
tune restart minima or AI coefficients from them without a separate justified change.
