# Tactical AI contract

Status: implemented through TACT-8. This document is the normative description
of the FM-style tactical AI: data ownership, units, inheritance, the coach tool
surface, error codes and the deliberately deferred surface. The source tree and
`AGENT.md` remain authoritative where they disagree with prose.

See `docs/tactical_ai_audit.md` for the pre-migration baseline and the
concept/licence mapping.

## 1. Ownership and data flow

```
Simulation::Observe() -> WorldState (value, 100 Hz)
                            |
                    AIEngine::Update(world, out)
                       |                 |
             CoachRuntime                Player AI
    (queue, shouts, revisions)     (reads boards + context)
                       |                 |
              transactional        PlayerControlSet
              board mutation             |
                       |        Simulation::Step(out)
             TacticalBoard[2] <----------+
```

| state | owner | mutation entry | readers |
| --- | --- | --- | --- |
| players/ball/rules/score | Simulation | `Step`, Rules | WorldState |
| persistent tactical plan | **AIEngine** (single authority) | `CoachRuntime` via `CoachCommandExecutor` | Player AI, coach tools |
| board revision | AIEngine board | executor commit | executor, coach log |
| transient shouts + mental effects | `CoachRuntime` | `ApplyShout` | Player AI context, `get_*` |
| attack/defence assignment, memory | Player policy | its own `Update` | that policy |
| actual motion | Player/Simulation | `PlayerControlSet` | simulation |
| model session / network | future CoachAgent | agent loop | coach only |

There is exactly one persistent board per side. `AIEngine` owns it; the coach
runtime holds a pointer and never copies it. `EffectiveTactics` is a derived
interpretation, never a second board.

## 2. Units and coordinates

- Simulation world metres, centre origin. `WorldTeamState.defending_direction`
  is the observed truth; the AI never assumes a per-half sign.
- Persistent plan uses canonical tactical coordinates:
  - `u` in `[0,1]`: 0 = own goal line, 1 = opponent goal line.
  - `v` in `[-1,1]`: -1 = own right touchline, +1 = own left touchline, viewed
    looking towards the opponent goal.
- Conversion (single implementation): `TacticalAnchorToWorld` /
  `WorldPositionToAnchor` in `tactical_interpreter.hpp`, with
  `attack_sign = -defending_direction`.
- The interpreter produces intent targets in world metres. It never writes a
  `Player` position; `BuildPlayerCommands` remains the only coordinate
  conversion into execution.

## 3. Board modes

`TacticalBoard::mode` is explicit and never inferred:

- `Legacy`: the historical `width`/`depth`/`players` fields. Byte-identical
  behaviour; coach writes are rejected with `LegacyBoard`.
- `V2`: `advanced` holds a `TacticalPlanV2`; the legacy fields are ignored.
  `ValidateTacticalBoard` enforces the pairing.

The Default player policy still matches the public `DefaultAI` baseline
(`default_policy_equivalence_test`).

## 4. Plan schema and value ranges

`TacticalPlanV2` (see `tactical_plan.hpp`):

| field | range | meaning |
| --- | --- | --- |
| `mentality` | enum | preset **source**, not a per-tick multiplier |
| `in_possession.attacking_width` | [0,1] | lateral spread while attacking |
| `in_possession.tempo` | [0,1] | quicker ball release |
| `in_possession.passing_directness` | [0,1] | forward/long preference |
| `in_possession.risk_taking` | [0,1] | reward vs turnover |
| `in_possession.creative_freedom` | [0,1] | deviation from the anchor |
| `in_possession.attacking_bias` | [0,1] | commitment forward |
| `in_possession.build_up` | enum | short/balanced/direct |
| `in_possession.pass_into_space` | bool | space vs feet |
| `out_of_possession.defensive_line` | [0,1] | block depth |
| `out_of_possession.line_of_engagement` | [0,1] | press start height (separate from the line) |
| `out_of_possession.pressing_intensity` | [0,1] | engagement strength |
| `out_of_possession.compactness` | [0,1] | block narrowness |
| `out_of_possession.marking` | enum | zonal/mixed/man |
| `out_of_possession.offside_trap` | bool | co-operative step (rules still own offside) |
| `transition.on_win` / `on_loss` | enum | counter/hold, counter-press/regroup |

`ValidateTacticalPlan` rejects non-finite values, out-of-range values, invalid
enums, out-of-range or duplicate slot ids and invalid marking targets. An
explicit `0` differs from "unset" for individual instructions (`std::nullopt`).

## 5. Inheritance and precedence

1. **Mentality preset** fills the concrete numeric team fields at plan creation
   or when a coach explicitly sets `Mentality`. It is never re-applied per tick.
2. **Team instruction** values are the base.
3. **Role profile** adds a bounded offset / spatial constraint; it never
   replaces the team value.
4. **Individual instruction** (by slot) is the final per-player override.
5. **Transient mental effect** (TACT-6b) tilts tendencies within
   `[0,1]`; it never changes base abilities or the plan.

## 6. Slot identity and player binding

- `TacticalSlotId` is `0..10` and is not a `PlayerId`.
- `SlotAssignment` maps slots to players and lives outside the persistent plan.
  The current default binding is observation order (`DefaultSlotAssignment`).
- Individual instructions inherit per slot; a future per-`PlayerId` override
  must be a separate map with a defined, higher precedence.

## 7. Coach tools

`CoachToolRegistry::Tools()` returns the descriptor list. v1 exposes nine tools:

| name | mutating | argument summary |
| --- | --- | --- |
| `get_match_state` | no | tick, phase, score, possession facts |
| `get_tactical_board` | no | own V2 plan (copy) |
| `get_player_info` | no | own player's observed values |
| `set_team_instruction` | yes | `InstructionKey` + typed `InstructionValue` |
| `set_slot_role` | yes | slot, phase, role |
| `set_slot_anchor` | yes | slot, phase, anchor |
| `set_individual_instruction` | yes | slot, `IndividualKey` + value |
| `set_transition_instruction` | yes | `TransitionKey` + value |
| `shout_to_players` | yes | side, `ShoutType`, targets |

Read-only tools expose only the interpreting coach's own side. There is no tool
to read the opponent's private plan, and no `substitute_player` in v1.

## 8. Command validation and commit

`CoachCommandEnvelope` carries `id`, `observed_tick`, `expires_at_tick`,
`expected_board_revision` and the typed `CoachCommand`.

`CoachRuntime::ApplyAtTick(tick, world)` runs at the **top of the next**
`AIEngine::Update` and:

1. prunes expired shouts;
2. for each queued command, rejects duplicate ids, then routes board writes to
   `CoachCommandExecutor` and shouts to `ApplyShout`;
3. logs a `CoachCommandRecord` and never applies an invalid command.

The executor validates, mutates a **copy** of the plan, re-validates and only
then commits and bumps `board.revision`. A failure leaves the board and its
revision untouched. Errors are never silently clamped.

Error codes: `StaleRevision`, `Expired`, `FutureTick`, `DuplicateId`,
`InvalidPhase`, `LegacyBoard`, `InvalidSlot`, `InvalidEnum`, `NonFiniteValue`,
`ValueOutOfRange`, `UnknownPlayer`, `WrongTeam`, `InvalidMarkingTarget`,
`Unsupported`, `MalformedCommand`, `NoShoutTargets`, `ShoutCooldown`.

Log records contain `{match_instance_id, call_id, observed_tick, applied_tick,
side, before_revision, after_revision, command, result}` and are pure data.

## 9. Shouts

- `ActiveShout{side, target, type, issued_tick, expires_at_tick,
  response_strength}` lives in `CoachRuntime`, never in the board or
  `WorldState`.
- A shout yields a bounded `PlayerMentalEffect{motivation, confidence, focus,
  composure}` that decays linearly to zero at expiry.
- Cooldown and a per-player stack cap are configurable; targets must be active
  and on the issuing side. A repeated same-type shout refreshes.
- v1 has no personality model: the same shout has the same bounded effect on
  everyone. Per-player variation is deferred until the observation supplies
  real emotional data.

## 10. Versioned player-policy interface

- `IPlayerPolicy` (frozen P21) is the legacy input: `WorldState` + boards.
- `IPlayerPolicyV2` takes `PlayerAIContext{world, tactics, mental_effects}`.
- `LegacyPlayerPolicyAdapter` drives an `IPlayerPolicy` under the V2 interface
  with no mental effects, preserving historical behaviour.
- `HeuristicPolicy` implements `IPlayerPolicyV2` and is a new
  `PlayerPolicyKind::Heuristic`; it is never the default.

## 11. Determinism and bounds

- No RNG in the AI layer: same world + boards + policy memory => same output.
- `Reset()` clears all per-`PlayerId` memory.
- Policies produce a complete control set every 100 Hz tick; speeds are bounded
  by the observed `max_speed`.
- The interpreter and `EffectiveTactics` are pure functions of their inputs.

## 12. Deliberately unsupported / deferred

- Complex set-piece routines, opponent weak-foot instructions and automatic
  substitutions (a future `substitute_player` would be validated by
  Simulation/Rules, not the board).
- Deep per-player psychology/personality and long-term coach memory.
- `pass_into_space` and `build_up` are carried in the schema but not yet
  consumed by Eliza; the heuristic consumes the interpreted anchors and mental
  effects. They are marked unsupported rather than faked.
- Real LLM/provider/network integration, tool-call JSON parsing and async
  submission. `ICoachPolicy` is the seam; v1 ships only deterministic policies
  (`HeuristicCoachPolicy` + NoOp).
- `TacticalOverlay` for Player12-style tactical shouts (press-more /
  shoot-on-sight). FM-style psychological shouts are implemented instead.
