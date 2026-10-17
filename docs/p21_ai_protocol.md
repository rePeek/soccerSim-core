# P21 AI <-> Player protocol

Frozen at P21.1 and exercised through P21.7. It is a value contract: the AI owns
decisions, the Player owns execution.

## Data flow

```
GameEnv
  ├── AIEngine
  │     ├── TacticalBoard[2]        coach intent, owned by the engine
  │     └── IPlayerPolicy           Default / Eliza / future GNN
  │           reads  : WorldState
  │           writes : PlayerControlSet
  └── Simulation::Step(controls)
        └── Player::SetControl -> BuildPlayerCommands -> arbitration ->
            install -> Animation::Match -> PlayerMotion -> Physics / Rules
```

## Input contract

- `WorldState` is the only observation. It is a value snapshot in one pitch
  frame (home defends -x), valid in both halves and both processing orders.
- `TacticalBoard` is the coach's persistent intent. The policy reads it, never
  writes it. `AIEngine` owns the boards and passes them read-only.
- The policy never holds `Player*`, `Team*`, `Match*`, `Simulation*`,
  `PlayerActionState`, `MentalImage` or `PlayerDefinition`, and never calls
  `Player::Play`/`React` or `Animation::Match`.

## Output contract

`IPlayerPolicy::Update(world, tactics, output)` fully replaces `output` for the
tick. Output is attributed by `PlayerId`:

| field | meaning |
| --- | --- |
| `player` | target `PlayerId` |
| `move_direction` | continuous movement in the public pitch frame |
| `desired_speed` | requested speed, m/s |
| `look_at` | desired facing/observation point |
| `action` | this tick's discrete action intent |
| `target_player` | pass recipient |
| `target_position` | pass/shot target point |
| `power` | execution-layer power, unchanged semantics |

`ControlAction::None` means "no new discrete action this tick"; it does not
cancel an installed action. A discrete action is an intent: the Player may
reject, defer or fail to match it. A player missing from the output keeps the
simulation's idle fallback; the previous tick's input is not implicitly carried
over. Coordinates are converted exactly once, in `BuildPlayerCommands`.

## Frequency contract

- `GameEnv::Step` calls `AIEngine::Update` exactly once per simulation tick
  (100 Hz), and `Simulation::Step` immediately after.
- The policy produces a complete `PlayerControlSet` every tick. It may keep
  per-`PlayerId` memory across ticks; `Reset()` clears it.
- The Player's own decision clock (the 20-240 ms relation-driven refresh) is
  **not** changed by P21. The AI never sets, reads or resets it.
- Duplicate discrete intents are bounded by the existing Player arbitration.
  There is **no** reliable one-shot action delivery guarantee: re-issuing the
  same intent is allowed and expected.

## Action feedback boundary

`Player::Play()` returns a `RequestOutcome`, but that entry is not the same path
as `PlayerControl -> BuildPlayerCommands()`. P21 therefore does **not** treat
`LastActionRequestOutcome()` as a general ACK, and does not add `request_id`,
expiry or resend logic. Any future feedback must be produced by the real
control execution path, not by a `Play()`-only counter.

## Future low-frequency policy (GNN)

Defined here, not implemented in P21:

```
low-frequency policy decision (e.g. 10-20 Hz)
        |
        v
per-player latest continuous intent (kept by the policy or the engine)
        |
        v
every-tick submission (100 Hz)
        |
        v
Simulation / Player
```

Discrete actions may later add request identity, validity windows, and
received / installed / executed / expired feedback. That is a separate protocol
extension; it must not require changing the Player's arbitration or the
Animation boundary.

## Frozen in P21

- `IPlayerPolicy`, `PlayerPolicyKind`, `AIConfig::player_policy`.
- `AIEngine` owns boards + one policy; `GameEnv` holds `AIEngine`.
- The Default baseline is field-identical to the pre-P21 behaviour.
- Eliza drives all 22 players through the same protocol, deterministically and
  without RNG.

## Deliberately deferred

- `request_id` / ACK / resend for discrete actions.
- Bounded public action-candidate lists (legacy ordered `PlayerCommand`
  fallbacks).
- Promoting process-global AI diagnostics to a Simulation-scoped census.
