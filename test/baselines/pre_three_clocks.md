# Before S2: three clocks / scale removal

Authoritative pre-stage commit: `2d2a490` (S1). S2 is an intentional football-time,
contact-release and fatigue semantic change, not a representation-only migration.
No asset, physics coefficient, animation rate or restart min/max was calibrated.

## Archived regression rows

`{executed steps, World digest, motion/action/RNG digest}`:

```cpp
{1,    UINT64_C(181739816817067074),  UINT64_C(350760935552669674)},
{100,  UINT64_C(2189927517799612813), UINT64_C(13725550824419574755)},
{500,  UINT64_C(1266667866899682051), UINT64_C(7040189435179413637)},
{1000, UINT64_C(6003776441005663363), UINT64_C(10968404906479542722)},
```

New S2 rows:

```cpp
{1,    UINT64_C(10332355978522980402), UINT64_C(350760935552669674)},
{100,  UINT64_C(6566733075672102469),  UINT64_C(13725550824419574755)},
{500,  UINT64_C(3072497800688270887),  UINT64_C(12665720491277525189)},
{1000, UINT64_C(15732669318329480461), UINT64_C(9316031574527648864)},
```

World hashing replaces `match_time_ms` with native regulation/ball-in-play durations
and their running facts. Thus even the first World rows differ. The independent
motion/action/RNG encoding is unchanged: steps 1/100 match exactly; 500/1000 expose
real changes rather than concealing them behind the observation schema.

## Archived identity/scheduling rows

`{unequal roster, reverse order, after 300 steps, after sendoff + 300 steps}`:

```cpp
{false, false, UINT64_C(13711565807151734712), UINT64_C(17201809211116718618)},
{false, true,  UINT64_C(16717170963448916555), UINT64_C(13813028668665107254)},
{true,  false, UINT64_C(7907520300668161649),  UINT64_C(12681165719454811576)},
{true,  true,  UINT64_C(13287372985434084791), UINT64_C(12082599955711407290)},
```

New S2 rows:

```cpp
{false, false, UINT64_C(16144338831632478706), UINT64_C(11101384966440775508)},
{false, true,  UINT64_C(1278980483242646393),  UINT64_C(2155356947635217068)},
{true,  false, UINT64_C(5415142161785239299),  UINT64_C(12038476573473573858)},
{true,  true,  UINT64_C(8110407433742795352),  UINT64_C(14851915034266500703)},
```

This digest's encoding is also unchanged: motion, action/caches, fatigue and RNG,
not the new clock fields. Before S2, a kickoff became ordinary play at the whistle
without requiring contact. Now Ready freezes non-takers and allows only the taker
to contact the ball; the same step budget is intentionally not equivalent gameplay.
The former reduced-roster step-207 free kick is not a S2 invariant. Its S1 evidence
remains in `pre_condition_restarts.md`. Separate ID-binding/affine-ID-equivalence
fixtures run 800 instead of 600 steps so they inspect movement after actual kickoff,
not the authorized non-taker freeze; scheduling snapshots stay at 300/600.

## Causal evidence and clock boundaries

Native default seed 42, normal order, one-tick halves (also covered without pinned
contact latency in lifecycle tests):

| Observed timeline | Phase | Authorization | Restart | Regulation | Ball-in-play |
|---:|---|---|---|---:|---:|
| 201 | first | yes | Ready | 0 | 0 |
| 601 | first | yes | Ready | 0 | 0 |
| 634 | first | yes | Taken | 1 | 1 |
| 635 | second ceremony | no | none | 1 | 1 |
| 665 | second | yes | Ready | 1 | 1 |

The first taker is ID 6, starting about 12.39 m from centre. Actual scheduled pass
contact occurs at action frame/contact 27, with moving ball; cursor expiry alone
cannot release play. This is fixture evidence, **not calibrated kickoff latency**.
Half-time pre-preparation ticks no longer execute unauthorized actor warmup; only
the ceremonial placement tail executes. Schedule/placement deadlines are unchanged.

The old default factor `.027 * .2 + .05` compressed each 10 ms physical tick into
180 ms of football time and compensated fatigue by its reciprocal. S2 instead
admits one regulation tick per physical tick during an underway half, includes
ordinary dead balls, excludes both ceremonies and removes inverse-scale fatigue.
A direct advance clips at the current half; the next rule call whistles even if
Pending/Ready/Taken. Native one-tick halves replace former sub-tick configurations.

Lifecycle coverage verifies both processing orders, no-input kickoff waits,
actual scheduled release, dead-ball accumulation, halftime/fulltime freezes,
period-over-pending precedence, integer precision, overflow atomicity and result
ownership. A native per-player movement test checks exact per-metre fatigue in
ordinary dead balls and no fatigue in ceremonial warmup. No possession/physics
formula or continuous reachability estimate was bulk-converted.

## Full-match and verification

- S1 default: **1–2 / away_win / 34097 executed calls**.
- S2 default: **27–26 / home_win / 541076 executed calls**.
- S2 180-tick halves: **0–0 / draw / 1436 executed calls**.

The large change is expected from removing compression, changing dead-ball clock
policy, requiring kickoff contact and removing fatigue compensation. It is not a
claim of football realism; multi-seed restart/effective-time measurement and
calibration remain separate work.

Release, Debug and true NDEBUG each pass 27/27 CTest registrations, including the
full default match. All four core/identity rows and full/short results agree across
modes. Core-only builds pass. Unpatched BqLog/native Debug assertions are retained.
Baked SHA256 remains:
`33ab837652da93a795e4886738ca09b92e4b6376c99a2500202572e0a7885b86`.
