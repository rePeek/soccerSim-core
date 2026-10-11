# P4f-3 active-touch calibration

## P4f-3-a: real baked boot contact point (committed)

The active touch model no longer derives the ball surface contact point from
the animation's desired ball centre plus a provisional technique offset only.
`PreparedPlayerTouch` now carries the real boot geometry captured during the one
Humanoid preparation pass:

- `Humanoid::notify_touch` reads the committed baked pose at the contact frame
  and extracts the kicking boot capsule (`BuildBakedContactRegions`, LeftBoot or
  RightBoot) chosen from the clip's `current_foot` (e_Foot 0/1). The shipped
  asset's `touch_foot` string is empty for all Shot/Pass clips, so the selector's
  `current_foot` is the authoritative foot identity, not `touch_foot`.
- `strike_point` is the boot surface point nearest the ball centre in the tick
  common frame; `strike_radius` is that boot's radius and doubles as the
  presence flag (zero means the legacy desired-centre fallback).
- `ProposeActiveTouch` now has a foot branch: reachability is the foot-surface
  to ball-surface gap (`|foot-ball| - ballR - bootR`), height is the boot vs
  ball centre gap, and the ball contact point is the near-side surface point
  toward the boot (`ball + normalize(boot-ball)*ballR`). Lateral/vertical
  technique offsets still add deliberate spin around that real base point.
  A far ball is rejected (Distance), never projected onto the boot.

The gratuitous arbitrary projection of a far ball onto the foot is gone: a
rejected proposal produces no impulse and no contact point.

### Verification

- `test/player_active_touch_model_test.cpp [active-touch][model][foot]`: near-side
  contact point, torque-free side strike, below-centre boot produces backspin,
  far-ball rejection, velocity error bound.
- `[prepared-tick]` ON/ON still produces both CCD body touches and active strikes
  in both processing orders (9 case batch, 982k assertions).
- A focused instrumental run confirmed the foot path is live in native play:
  the prepared proposals carry `strike_radius` 0.063-0.070 (scaled boot radius)
  with both `strike_foot` 0 (left) and 1 (right), not the zero fallback.

## Remaining P4f-3 gates (not complete)

- P4f-3-b active/passive boot-conflict policy: one physical authority per kick,
  no double impulse when the real boot CCD precedes the animation contact frame,
  support foot never treated as the deliberate kick.
- Per-action calibration (ShortPass/LongPass/HighPass/Shot first): contact-point
  distribution, reach/rejection rates, linear/spin magnitude, 0.5/1/2 s flight,
  left/right mirror, and the documented old-vs-new trajectory delta.
- Header/Sliding/Goalkeeper-save classification and migration status (must be at
  least categorised and covered, not silently relying on undocumented legacy
  state mutation).

Defaults remain OFF; no Golden/asset change.