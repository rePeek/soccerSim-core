# Pre-value-AI reference (7dd3c63)

These are historical references, not the current passing expectations. Phase 5
replaces actor-calling Eliza/TeamAIController with a stateless value policy; it is
an intentional policy/trajectory/RNG-consumption change, not a mechanical move.
No dummy AI RNG draws preserve the old trajectory. Observations now use a common
home pitch frame and include ball motion, play/restart/retention and team state;
the current world hash enumerates those fields too.

## Old WorldState / simulation-digest pairs

| Tick | World hash | Simulation hash |
| --- | --- | --- |
| 1 | 9302794185408323355 | 12821310424240230946 |
| 100 | 4089301356990025244 | 11211753855030439651 |
| 500 | 4132523896133699106 | 15669018830692089552 |
| 1000 | 11167980816582236617 | 12759021268043178079 |

Old animation A/B:

```text
core_animation_ab mode=foot_order event_tick=10 first_difference=-1
core_animation_ab mode=frame_count event_tick=133 first_difference=133
```

Old scheduling/send-off fingerprints (300 ticks + send-off + 300 ticks):

| Rosters | Processing | Before | After |
| --- | --- | --- | --- |
| Equal | Normal | 1081277329640161974 | 2153253164783353558 |
| Equal | Reversed | 13929020379921652592 | 13989442576941677585 |
| Unequal | Normal | 5031617072857338529 | 17380964153918716995 |
| Unequal | Reversed | 10672886534341869106 | 15776948655175517899 |

## What remains exact

Rules keep their clocks, card budgets, restart reseed/order, actor placement,
selection and retain-animation mechanics. An independently compiled original
`TeamAIController::PrepareSetPiece` plus original formation policy was compared
with `PositionRestartPlayers` after each of 72 cases: six restart types, both
processing orders, both taker teams, and 11/11, 3/2, 1/1 rosters. Exact float,
action/retention/selection and RNG fingerprints matched; the captured original
aggregate baselines live in `test/restart_placement_test.cpp`.

Reachability/query/kick baselines, actor reset/destruction multiplicities,
identity independence and animation mechanics remain independently tested.
Current policy goldens live in `tools/football_regression.cpp`; current
scheduling fingerprints live in `tools/player_identity_test.cpp`. Both expose
`--print-baseline`; normal test runs always check their fixed expectations.
