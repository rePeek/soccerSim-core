# P4f/P6c/P7 final delivery report (local, no push)

Baseline `1e4f547` (main). All work below is local commits, Debug/core-only;
no Release/app/CLI acceptance, no default flip, no Golden/asset/Ball-model
change. Legacy/Motion smoothing work respects AGENT.md.

## 1. Local commits since 1e4f547

```text
0ed78bd fix(sim): diagnose Movement entry-smoothing catch-up and drop slerp lag (P4f-2c-a)
33d08f3 build: enable ccache launcher, use all cores, document test-loop perf
7b91e92 feat(sim): derive active-touch contact from real baked boot (P4f-3-a)
53328a3 fix(sim): gate deliberate boot kicks on real swing speed (P6c-a)
b0fe054 test(referee): deliberate defender play clears offside candidate (P6c-b)
81b0fe5 fix(sim): pick the boot nearest the ball for real foot contact (P4f-3-a)
```

## 2. Phase status

- P4f-2c: **partial** — 2c-a (motion decomposition + slerp-lag removal) done;
  2c-b (15-collider per-action acceptance matrix) not yet written.
- P4f-3: **partial** — 3-a (real boot contact point + nearest-boot + foot reach)
  done; 3-b (active/passive conflict calibration + per-action flight table)
  documented, not yet measured.
- P6c: **partial** — P6c-a swing gate done; P6c-b offside deliberate-play test
  done; out-of-bounds/kickoff/goal/keeper/lifecycle matrix not yet written.
- P7a: **not executed** (maintainer gate) — checklist in
  `tools/p7a-promotion-gates.md`.
- P7b: **baseline recorded** — `tools/p7b-pre-promotion-baseline.md`.
- P7c: **blocked on P7a** — plan in `tools/p7c-legacy-removal-plan.md`.

## 3. Default production authority

Still **OFF**. `body_physics_production_`, `prepared_tick_production_` and
`baked_body_production_` in `src/sim/simulation.hpp` remain `false`.

## 4. Final Ball API

Unchanged module boundary. Kept: `Ball::Step(BallTickInput)`, `state()`,
`Predict(BallTickInput) const`, `Predict(TickSpan, BallEnvironment) const`,
`Reset`, `Mirror()`, read-only `config()`. Legacy member entries
(`Touch`/`SetRotation`/`ApplyForce`/`ApplyImpulse`/`BallSpatialInfo`/old
duration `Step`) remain only because the default path still calls them; they
are the P7c deletion list.

## 5. 15-collider conclusions

- Provider tape builds all 6 primary + 9 contact regions from the committed
  realized skeleton; IDs stay in the reserved bands.
- Dense 1/16-Tick linear-vs-quaternion error: raw ~0.0113 m, realized ~0.0186 m
  (measured, not an approved tolerance).
- The 0.215 m/Tick Movement catch-up is decomposed (P4f-2c-a): raw clip 1147
  gait is 4.4-7.5 m/s; the realized peak is entry-smoothing catch-up after a
  clip switch, bounded by the inherited 7.5*pi rad/s joint rate. Slerp bias
  removed; no collider-only clamp introduced.

## 6. Active-touch contact/spin/flight

- Contact point = near-side ball surface toward the nearest real boot; spin
  from `ApplyImpulseAtPoint` on that lever arm (no spin assignment).
- Reach = boot-surface to ball-surface gap (0.25 m) + foot height gate (0.8 m);
  far balls reject instead of being projected onto the foot.
- `[active-touch][model][foot]` covers near-side contact, below-centre backspin,
  far-ball rejection and centre-strike torque. No per-action flight table yet.

## 7. CCD RuleTouch -> Referee

- Path: `BallStepResult` -> ColliderId owner -> `PublishBodyRuleTouches` ->
  `AcceptedTouchSink` -> `Referee::EvaluateBallTouch`.
- Swing gate: only a boot swinging >4 m/s relative to the player root is
  `Intentional_Kicked`/`physical_kick`; a planted support foot stays Accidental.
- Ready release still requires the taker + kick action + live ball.
- Offside: passive deflection preserves the candidate; deliberate defender play
  clears it; attacker touch still triggers the verdict (tests pass).

## 8. Debug/core / acceptance

- Debug focused runs pass: `[baked-pose]`, `[pose-interpolation]`,
  `[pose-runtime]`, `[pose-realization]`, `[pose-motion]`, `[baked-regions]`,
  `[mesh-envelope]`, `[baked-pipeline]`, `[prepared-tick]`, `[rule-touch]`,
  `[active-touch]`, `[active-shadow]`.
- Full Debug suite was NOT rerun after the latest fixes (per instruction to
  defer the full-suite run until P7 completes); one P5c regression found during
  iteration was fixed (nearest-boot selection).
- Core-only build passes.
- Release/full-match/new Golden: NOT run (maintainer authorization required).

## 9. Remaining unmet gates / blockers

1. P4f-2c-b real-action 15-collider scenario matrix.
2. P4f-3-b active/passive conflict + per-action flight calibration table.
3. P6c-b out-of-bounds/kickoff/goal/keeper/lifecycle rule matrix.
4. P7a default flip + full-match/Release/Golden (maintainer authorization).
5. P7c legacy deletion (blocks on 4).

## 10. Workspace state

Clean except for the untracked `tools/p7*.md` notes; no staged edits, no push.