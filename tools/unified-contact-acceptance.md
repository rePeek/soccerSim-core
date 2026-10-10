# Unified contact acceptance ledger

## Scope (experimental, defaults OFF)

- `0667bd0`: prepare actors once, arbitrate and commit the endpoint impulse or retain constraint through one normal Ball Step, then publish/commit actor consequences.
- `c8c224f`: source-tagged RuleTouch after commit, geometric-episode deduplication, identity mapping and referee offside-deflection handling.
- `c3e3835`: Baked local-pose FK and anatomical capsules/sphere; **geometry foundation only**.
- `9b5a444`: remove the immediate impulse API/adapter; const preparation view; current-Tick lifecycle guards; actual committed endpoint evidence; terminal replay/latency tool.
- Prepared Shadow additionally observes cached real proposals and their exact winning point impulse without a second action, RNG draw or Ball Step. Legacy surface-projection flight diagnostics are not zero-offset-model calibration.

The default fused path remains for preserving existing trajectories and Goldens. This is not a completed default promotion or Legacy removal.

## Timing replacement

All preparing actors read the same passive endpoint and prior committed touch history. They do **not** synchronously see earlier proposals as accepted touches. The newest mental image is captured from the passive endpoint before preparation and its ball predictions refreshed after commit. Referee/event publication happens after the authoritative Ball Step. Fatigue/send-off commit does not repeat animation or action RNG.

Native ON/ON now uses real current-Tick endpoint impulses, not a diagnostic retained winner or a next-Tick queue. Zero-impulse corrections do not suppress a strike; genuine passive impact (including static impact) takes priority. Retain anchoring is an exclusive constraint, with only acquisition creating a new touch.

## Completed checks

- Final Release CTest: 42/42, 380.11 s. This duration is **not** a tick-latency measurement.
- Debug suite after immediate-API/const-view cleanup: 41/41 (excluding full regulation CLI), 741.01 s. After prepared-Shadow hardening, Release and Debug focused tests both pass 1,713,450 assertions / 15 cases.
- Core-only build passes. Final default-OFF regression fingerprint is byte-identical to `/tmp/ball-7e71690-baseline.txt`; existing regression/A-B CTests pass.
- New prepared-Shadow enabled/disabled ON/ON replay: 215,716 assertions / 1 case, both processing orders, 2,200 steps each. Every reported executed impulse/point equals the committed input; physics, actor frames/positions and RNG are unchanged by observation.
- Baked pose test covers every Sliding/Trip frame, local hierarchy, root-motion ownership and mirror geometry. This is not production pose agreement.
- Initial short terminal ON/ON runs (20 seconds per half, not regulation acceptance): normal 5,038 steps, 15 CCD rule touches / 15 active touches / 116 constraint ticks; reverse 5,039 steps, 1 / 7 / 0. Exact replay. Normal mean/p99/max Step 1555.07/2014.21/3312.91 us; reverse 2048.9/2708.4/3400.22 us. These are measurements of that run/environment, not guaranteed worst-case latency.

### Native 90-minute prototype replay (both completed)

| Order | Executed steps | Snapshot/touch hash | CCD rule touches | Active rule touches | Constraint ticks | Mean / p99 / max Step (us) |
|---|---:|---:|---:|---:|---:|---:|
| normal | 541038 | 14748828304581481064 | 121 | 145 | 1136 | 1406 / 2128.02 / 5193.38 |
| reverse | 541039 | 9589753637340935145 | 144 | 194 | 537 | 1979.96 / 3029.5 / 6905.78 |

Each run used `half_ticks=270000` (45 minutes), reached Referee terminal state,
and replayed exactly for the hashed projections/touch fields and final RNG.
These runs were launched after const-view/immediate-API cleanup but before the
subsequent lifecycle, exact-proposal-provenance and prepared-Shadow hardening.
They verify that prototype, **not** an exact full-match regression hash for
HEAD `6db34f9`. The final source additionally passes the suites listed above.
Full-regulation rerun/review of the final calibrated implementation is still a
promotion gate; these results must not be relabelled as a final new Golden.
Latency is measured in this shared test environment and is not a worst-case
real-time guarantee. Full logs: `/tmp/unified-full-normal.log` and
`/tmp/unified-full-reverse.log`.

## Reproduce

```sh
nix develop --command bash -c 'cmake --build --preset release -j 4 && ctest --preset release -j 4'
nix develop --command bash -c 'cmake --build --preset debug -j 4 && ctest --preset debug -j 4 -E "^football_app_cli_default$"'
nix develop --command build/release/football_sim_computation_test '[prepared-tick],[rule-touch],[baked-pose],[active-shadow]'
nix develop --command build/release/football_unified_acceptance full
nix develop --command build/release/football_unified_acceptance full reverse
```

`full` means actual 45-minute halves, native AI, ON/ON, run until Referee terminal state and replay again. The tool measures only `Simulation::Step` latency; AI and hashing are outside that timing. Hashes cover snapshot ball/player projections and RuleTouch identity/type/source/stamps, with final RNG comparison. They are diagnostics, not a reviewed replacement Golden or a comprehensive football-rule oracle; orientation and all event/rule payloads are not yet included in that hash.

## Unmet acceptance gates

1. **P4f production pose motion:** Ball CCD assumes linearly translating capsules. Baked capsules rotate/deform; feeding them into that kernel would be incorrect. A swept endpoint/rotation contract and actual adjacent-Tick pose tapes are required before production use.
2. **P4f technique calibration:** prepared strikes currently use zero-offset point impulses. Actual foot/surface geometry, Player precision/power/foot mapping, spin maxima/direction, incoming-spin traps/return passes, reach/rejection rates and 0.5/1/2-second flight still need calibrated evidence. `J/m` target-velocity agreement is tautological.
3. **Football-rule acceptance:** the rule path is connected and scoped tests pass, but complete ON/ON ownership/order, continuous-contact episodes, offside, exits, goal/saves/restart attribution and possession need review with final posed geometry. A replay hash alone cannot accept these semantics.
4. **P7 promotion:** no reviewed new Golden. Do not flip defaults or remove the still-used legacy Touch/SetRotation/duration callers before the above gates. The newly introduced immediate point-impulse API was removed; that is only API convergence, not all Legacy removal.

No asset/data edit, Golden replacement or push accompanies this work. Animation asset SHA256 remains `33ab837652da93a795e4886738ca09b92e4b6376c99a2500202572e0a7885b86`.
