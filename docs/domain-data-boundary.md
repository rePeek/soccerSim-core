# Domain data boundary

Frozen principle:

    Entity  = Profile + State
    System  = reads Profile + current State -> writes new State
    Derived = recomputed from Profile + State + Context, never stored as authority

- **Profile** — immutable for a match. Inherent properties of the entity.
- **State** — copyable simulation snapshot: the dynamic data needed to restore
  one frame **given the same `WorldProfiles`**. Cheap to copy, predict and
  replay. A snapshot alone is not a complete world; restoring one frame needs
  `WorldProfiles + WorldState`, because profiles are not serialized here.
- **Derived** — recomputed on demand. If it can be computed from Profile +
  State + Context, it must not be an authoritative stored field.

This is the shared boundary for Physics, Contact, Action, GNN input, Replay and
the C API.

## Ownership

    World
    ├── WorldProfiles            (match-lifetime configuration)
    │   ├── BallProfile
    │   └── PlayerProfile[22]
    └── WorldState               (dynamic snapshot)
        ├── BallState
        ├── PlayerState[22]
        └── MatchState
Ownership note: `WorldProfiles` is still filled by `Match` during setup and
`World::GetProfiles()` still exposes a mutable reference for that migration.
The target is `CompileWorldProfiles(MatchData) -> WorldProfiles` followed by
a freeze, with `Match`/entities only reading it. Also note that a per-match
reset currently clears `players` but not `ball`; once a match can select a
different ball, the compile step must reset both.

`football::domain::Ball` and `football::domain::Player` are thin views binding a
profile reference to a state reference. They own no algorithms and copy no
data. Systems take `(profile, state)` and return a new state, e.g.
`BallPhysics::Step(state, dt, ball_profile, params, ...)`.

Predict/resolve relies on this split: `WorldState next = current;` copies only
dynamic state, never profiles. Committing a prediction assigns into the existing
storage so bound views and PlayerBase aliases stay valid — never swap the
`WorldState` object itself.

Ownership status: `BallState` and the first 11 `PlayerState` slots per team are
World-owned. Officials and extra bench players keep local state. `MatchState`
still migrates.

## Ball

Current target schema:

    struct BallProfile { float radius; float mass; /* later: inertia, aero */ };
    struct BallState   { Vector3 position; Vector3 velocity;
                         Vector3 angularVelocity; Quaternion orientation; };

Known legacy naming debt, deliberately not renamed yet (it would change the
save-state stream and every reader at once): `BallState::momentum` is a
velocity in m/s, and `BallState::rotation_ms` is a per-axis rotation, not an
angular velocity. Rename them in a dedicated schema-cleanup commit.

Not ball profile: gravity, grass/pitch, air properties (Environment) and
restitution/friction (contact material pairs). `BallPhysicsParams` currently
mixes these three categories; it is a transitional struct, not the target
schema.

## Player

`PlayerProfile` starts with the physical group needed for movement and contact.
Technical, mental and personality groups are added by the Action and AI phases
and all belong to the same profile. `PlayerData` is the database/source format;
`MakeSimulationPlayerProfile` is the boundary that compiles it into simulation
properties, so the domain stays independent of database layout.

Important: `Player::GetStat()` is **not** a profile source. It multiplies the
database stat by AI difficulty and the current fatigue factor, i.e. match
context plus condition. Profile stores the inherent ability.

`PlayerState` currently holds kinematics
(`position`, `velocity`, `movementFacing`, `torsoFacing`). Condition
(`stamina`, `currentBalance`) and action state are dynamic too, but answer
different questions and will be split into `PlayerConditionState` /
`PlayerActionState` / `PlayerDecisionState` rather than one large struct.
Profile ability is not condition: `balance` is skill, `currentBalance` is how
stable the player is right now.

## Derived, not state

Speed (`|velocity|`), distance to ball, pressure, ETA, and collider geometry.

New contact types live in `namespace football::contact`, alongside
`football::domain`. Contact is a shared fact (Physics, Action, Rules/Events and
Replay all consume it), not a detail of one solver, so it gets its own
namespace at the same level as the domain entities. `Contact`,
`ContactDetector`, `ContactResolver`, `ContactMaterial` and further shapes
belong there.

The directory is `core/contact/`, aligned with the namespace (parallel to
`core/domain/` and `core/state/`). The geometry primitive
`football::contact::CircleCollider` lives in `core/contact/circle_collider.hpp`,
and `football::contact::BuildPlayerGroundCollider(profile, state)` in
`core/contact/player_collider.hpp` is the canonical mapping: `radius`
from `PlayerProfile::physical.bodyRadius`, `center` from
`PlayerState::position`. Core never includes `onthepitch`; legacy code
includes core, not the other way round.

`PlayerBase` keeps one `groundCollider` copy only as a serialized
compatibility shadow, projected from the authoritative inputs and checked
bit-exact by `CheckGroundColliderOracle()` at every mutation point and on
restore. Gameplay no longer reads it (`GetGroundCollider()` is now only for
save/load and the regression oracle); new simulation/contact code must use
`GetDerivedGroundCollider()` or the builder directly. `PlayerBodyCollider`
and `PlayerActionVolume` are still parameter structs rather than profile/state
derived.

## 7G contact guidance

The new resolver must not inherit the legacy authority shortcuts. In
`Match::CheckHumanoidCollision()` the penetration and offsets are still driven
by the hardcoded `bouncePlayerRadius = 0.5f * 0.72f` and by
`Player::GetStat(physical_balance)`. That is kept only for regression
exactness. New contact code uses `aProfile.physical.bodyRadius` /
`bProfile.physical.bodyRadius`, computes `penetration = rA + rB - distance`,
and takes stability from `Profile.balance` plus a future
`State.condition.currentBalance` — never from `GetStat()`, which folds in AI
difficulty and current fatigue.

## Not profile, not state

Team tactics / `TacticalBoard` (Coach output, match configuration) and
`EnvironmentProfile` (gravity, grass, pitch and goal geometry). GNN input is
therefore `WorldState + PlayerProfile + TeamContext`.

## Deferred

- Rename `BallState::momentum` -> `velocity`, `rotation_ms` ->
  `angularVelocity` (separate, save-format-visible commit).
- Split `BallPhysicsParams` into `BallProfile` / `EnvironmentProfile` /
  contact materials.
- Make `PlayerBodyCollider`/`PlayerActionVolume` derived from Profile + State
  (+ ActionState) instead of standalone parameter structs.
- Add `PlayerConditionState`, then technical/mental/personality profile groups.
- 7G contact work consumes `BallProfile`/`PlayerProfile` instead of literals.
