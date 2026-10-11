# P6c rule-semantic acceptance

## In scope

Review the CCD `RuleTouch -> AcceptedTouchSink -> Referee` path with the real
posed boot/boot-swing geometry, not the legacy marker timing.

## Production path

```
BallStepResult (single authoritative Step)
  -> ColliderId -> PlayerId/BodyPart (body_collider_owners_)
  -> Simulation::PublishBodyRuleTouches
      boot + authorized kick action + genuine swing => physical_kick
  -> AcceptedTouchSink -> Referee::EvaluateBallTouch
      Ready restart: taker + (scheduled_contact || physical_kick) + kick + live ball
```

## P6c-a: support-foot exclusion (swing gate)

`PublishBodyRuleTouches` previously promoted any boot CCD during an authorized
Shot/ShortPass/LongPass/HighPass to `Intentional_Kicked`/`physical_kick`. It now
additionally requires the boot to be actively swinging relative to the player's
root translation:

```
swing = |contact.surface_velocity - player.velocity| > 4.0 m/s
```

The planted support foot moves with the root (near-zero swing), so a support-foot
collision during a kick action stays `Accidental` and cannot release a Ready
restart or mark a deliberate play. The kicking boot swings at ~15-25 m/s and
keeps the deliberate classification. The referee path is unchanged: it only
trusts the `physical_kick` flag that Simulation now sets more precisely.

## Double resolution (P4f-3-b point 3)

`DecideContactAuthority` gives a real passive impact priority and drops the
active impulse, so a tick with a real boot CCD never also applies the planned
point impulse. The prepared-tick test asserts that a published BodyCCD touch
implies an empty `active_impulse` in the committed `BallStepResult`.

## Verification so far

- `[prepared-tick]` ON/ON: both processing orders still produce BodyCCD touches
  and PreparedAction strikes; no double impulse; deterministic replay.
- `[rule-touch]`: reviewed opponent deflection preserves offside candidates.

## Remaining P6c gates (not complete)

- Explicit support-foot vs kicking-foot scenario (slow planted boot contact is
  Accidental; fast swinging boot contact is Intentional_Kicked).
- Offside matrix: attacker offside candidate, passive deflection does not clear
  it, deliberate defender play updates it, offside attacker touch triggers a
  verdict, multi-part single tick does not duplicate the verdict.
- Out-of-bounds/corner/goal-kick/throw-in attribution and last-touch identity,
  post/ground never changes last-toucher, Ready taker vs non-taker vs early
  non-Ready touch.
- Keeper save/held-ball/no-duplicate-publish/release.
- Lifecycle: kickoff, half change, goal restart, penalties/set pieces, send-off,
  ResetSituation, Stop, post-finish Step.

Defaults remain OFF; no Golden/asset change.