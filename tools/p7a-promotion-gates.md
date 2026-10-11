# P7a default-promotion gates and order

Promotion is NOT executed here: the maintainer must authorize Release,
app/CLI and app-based acceptance per AGENT.md. This doc is the ready-to-run
checklist when that authorization arrives.

## Preconditions (must all hold)

- P4f-2c: committed realized pose drives all 15 colliders; no ghost contacts;
  real-action collision accounts are geometric; no per-tick double impulse.
- P4f-3: active-touch contact point from the real baked boot; spin from the
  point impulse; Shot/ShortPass/LongPass/HighPass flight targets are
  explainable; active/passive never double-fire; left/right mirror correct.
- P6c: swing-gated deliberate kick; Ready taker-only release; offside
  deflection/deliberate-play semantics; out-of-bounds/kickoff/goal/half/
  keeper/lifecycle scenarios pass; no Shadow dependence for normal results.
- Single authoritative `Ball::Step` per Tick; no Humanoid direct BallState edit.
- Replay determinism (same input -> identical State/RNG) in Normal/Reverse.

## Switch sites

`src/sim/simulation.hpp` private defaults, all `false` today:

- `body_physics_production_`
- `prepared_tick_production_`
- `baked_body_production_`

GAcked through `Simulation::EnableBodyPhysicsProduction`,
`EnablePreparedTickProduction` and `EnableBakedBodyProduction`. The callers in
`Simulation::Init`/`Step` (legacy fused path vs prepared path) select on these
flags; flipping the defaults is the promotion, keeping the OFF fallback
available only as a diagnostic comparison during the transition.

## Promotion order (independent commits)

1. Default `prepared_tick_production_ = true` (prepare/arbitrate/Step/commit +
   active impulse in the one Step).
2. Default `body_physics_production_ = true` (unified passive body CCD).
3. Default `baked_body_production_ = true` (15-shape committed skeleton tape).
4. Keep diagnostic Shadow/report switches OFF; verify Shadow ON == OFF physics.
5. Regression sweep: restart, goal, half change, send-off, terminal freeze,
   Stop/Init replay, both processing orders, several seeds.
6. Record the OFF->ON baseline transition (see p7b-pre-promotion-baseline.md)
   and the reviewed new Golden, then remove the comparison switches in P7c.

## Full-match acceptance (requires maintainer authorization)

```text
nix develop --command build/release/football_unified_acceptance full
nix develop --command build/release/football_unified_acceptance full reverse
nix develop --command bash -c 'cmake --preset release -DFOOTBALL_BUILD_APP=ON && \
  cmake --build --preset release -j $(nproc) && ctest --preset release -j $(nproc)'
```

Verify: executed steps per half, terminal state, exact replay of the hashed
projections/touches and final RNG, RuleTouch/EventLog/MatchResult, player
animation/identity, ball position/velocity/angular-velocity, finite numerics,
tick-latency distribution, no duplicate goals/infinite restarts.

## Stop condition

Do NOT promote if any gate fails. If blocked, keep the independent completed
commits, leave the defaults OFF and report the failing scenario.