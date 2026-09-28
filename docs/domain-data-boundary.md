# Domain data boundary

Frozen principle:

    Entity  = Profile + State
    System  = reads Profile + current State -> writes new State
    Derived = recomputed from Profile + State + Context, never stored as authority

- **Profile** — immutable for a match. Inherent properties of the entity.
- **State** — copyable simulation snapshot: everything needed to restore one
  frame. Cheap to copy, predict and replay.
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

`PlayerGroundCollider` is now built by `BuildPlayerGroundCollider(profile,
state)`: `radius` comes from `PlayerProfile::physical.bodyRadius`, `center`
from `PlayerState::position`. `PlayerBase` keeps one `groundCollider` copy
only as a serialized compatibility shadow, projected from the authoritative
inputs and checked bit-exact by `CheckGroundColliderOracle()` at every
mutation point and on restore. New simulation/contact code must use
`GetDerivedGroundCollider()`, never the shadow. `PlayerBodyCollider` and
`PlayerActionVolume` are still parameter structs rather than profile/state
derived.

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
