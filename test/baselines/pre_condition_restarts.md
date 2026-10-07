# Before condition-driven ordinary restarts

Historical baseline: `3f1f11a` (T3c), after the representation-only tick stages.
S1 intentionally changes football restart policy, not dt or time-unit arithmetic.

## Identity/scheduling policy fingerprints

Columns: unequal roster, reverse processing, before send-off (300 executed steps),
after send-off (another 300). The original fingerprints are retained here:

```cpp
{false, false, UINT64_C(13711565807151734712), UINT64_C(17201809211116718618)},
{false, true,  UINT64_C(16717170963448916555), UINT64_C(13813028668665107254)},
{true,  false, UINT64_C(1658563135750430224),  UINT64_C(10374616427843446362)},
{true,  true,  UINT64_C(13287372985434084791), UINT64_C(12082599955711407290)},
```

Only the normal-order 3/2-roster row changes under S1:

```cpp
{true, false, UINT64_C(7907520300668161649), UINT64_C(12681165719454811576)},
```

Causal trace of that fixture: at executed step **207**, Referee schedules an
ordinary **free kick** (mode 3), entered timeline tick **206**, scores **0–0**,
reset sequence **1**. It is still positioning at tick 300 before the send-off.
The old scheduler skipped 190 ticks, reset/teleported both teams at a preset
preparation deadline, then authorized at another fixed deadline. The new policy
executes positioning ticks, admits an already legally ready quick free kick
without an added minimum, and authorizes only after geometric readiness (or
explicit timeout recovery). Changed action/position/RNG fingerprints are expected
and pinned as a policy-version change, not hidden by a wholesale golden refresh.
The other three identity rows remain bit-identical.

## Unchanged core numerical/RNG golden pairs

These remain the **active** goldens in `tools/football_regression.cpp`:

```cpp
{1,    UINT64_C(181739816817067074),  UINT64_C(350760935552669674)},
{100,  UINT64_C(2189927517799612813), UINT64_C(13725550824419574755)},
{500,  UINT64_C(1266667866899682051), UINT64_C(7040189435179413637)},
{1000, UINT64_C(6003776441005663363), UINT64_C(10968404906479542722)},
```

No baked asset, importer or fixture quantization changes accompany S1. Historical
legacy placement's 72 fingerprints also remain active: it is still tested as
ceremonial placement, not used to teleport ordinary pending actors.

## Complete default match

- Before: `home_score=2 away_score=1 outcome=home_win duration_ticks=31041`.
- S1: `home_score=1 away_score=2 outcome=away_win duration_ticks=34097`.

This full-match delta is expected after changing ordinary football rules and the
post-goal kickoff taker ownership, not a claim of realism improvement. S1 still
uses the scaled football clock paused during dead balls and its fatigue factor.
Three-clock semantics, scale removal and multi-seed calibration remain separate
work. Minimum/maximum delays are provisional engineering bounds.
