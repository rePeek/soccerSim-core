# P7c Legacy removal plan (analysis only — blocked on P7a)

Deletion is deferred: the legacy producers below are still the DEFAULT path
(the switches in `src/sim/simulation.hpp` are OFF), so removing them first
would break the product. Run this only after P7a flips the unified defaults
and the full-match/Golden transition is reviewed.

## Legacy active-touch producers (default OFF path)

- `src/sim/simulation.cpp` `ResolveBallPlayerContacts` fused path:
  - `football::sim::ApplyBallTouch(...)` (line ~633)
  - `ball_->SetRotation(rng_.Uniform(-30,30), ...)` (line ~637)
- `src/sim/simulation.cpp` diagnostic escape around line ~814
  (`ApplyBallTouch` again).
- `src/sim/player/humanoid/humanoid.cpp` `legacy_ball` mutation port:
  - `tick.legacy_ball->SetRotation(...)` (line ~121)
  - `football::sim::ApplyBallTouch(*tick.legacy_ball, ...)` (line ~166)

## Legacy Ball member API candidates

- `Ball::Touch`, `Ball::SetMomentum`, `Ball::SetRotation`, `Ball::ApplyForce`,
  `Ball::ApplyImpulse`, `Ball::ApplyImpulseAtPoint` (immediate member entries),
  `BallSpatialInfo`, `CalculatePrediction`, the old duration `Step`/`Process`/
  `Prediction` compatibility interfaces.

Keep after P7c: `Ball::Step(const BallTickInput&)`, `state()`,
`Predict(const BallTickInput&) const`, `Predict(TickSpan, const BallEnvironment&)`,
`Reset`, `Mirror()`, and read-only `config()` if still used for physics queries.

## Legacy body-collision adapter removals

- Old `ResolveBallPlayerContacts` bounce path and random angular-velocity
  generation.
- Cooldown compat fields and history/adapters serving only the old path.
- Retired pose proposals and Shadow bridges that only existed for migration.
- Expired compat tests; keep the generic contact-evidence and deterministic
  regression tests that remain diagnostic.

## Keep (not Legacy)

- `AcceptedTouchSink`, `RuleTouch`/`BallStepResult` contact evidence, read-only
  prediction, lifecycle APIs, `Mirror()` (two-team coordinate mirroring).
- `Player`–`Player` collision/foul logic (independent of ball Legacy).

## Responsibility boundaries after P7c

- Ball: state, free motion, static/dynamic CCD, point-impulse, read-only predict.
- Player: baked pose, body colliders, motion/action, active-touch intent.
- Simulation: tick orchestration, physics input + identity mapping, contact
  arbitration, state commit, RuleTouch generation.
- Referee: consume touch facts; offside/foul/goal/out-of-play/restart verdicts.

Do not reintroduce upward Ball->Player/Referee/Animation dependencies during
the deletion.