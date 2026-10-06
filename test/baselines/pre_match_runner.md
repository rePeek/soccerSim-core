# World schema before autonomous match runner

Before `MatchPhase` and `match_time_ms` joined WorldState, the World hashes were:

| Steps | World hash | Simulation motion/actions/RNG digest (unchanged) |
|---:|---:|---:|
| 1 | 5371189051509720323 | 350760935552669674 |
| 100 | 1291515177572478404 | 13725550824419574755 |
| 500 | 1650659871221397832 | 7040189435179413637 |
| 1000 | 1772856281264394018 | 10968404906479542722 |

Only the observation hash schema changes at these checkpoints. Regulation now
ends after two configured halves instead of running indefinitely. Clock increments
retain the legacy per-step scaled/truncated value, but accumulate as uint64 and
clip at each period boundary (rather than re-rounding the accumulated float).
World.tick retains the compressed elapsed clock including restart skips;
MatchResult.duration_ticks counts actual executed Step calls, including the
referee's terminal transition, and excludes calls after completion.
Epoch identity remains outside deterministic physical hashes.
