# TACT-0: FM-style tactical AI audit and migration gates

Status: audit only. No behaviour change, no code moved.

This document freezes the starting point for the FM-style tactical AI work
(TACT-1..TACT-8). It records the current AI surface, the ownership split, the
external concept sources and their licences, and the concrete verification
gates each later phase must clear. The checked-out working tree and `AGENT.md`
are authoritative; where this document and the tree disagree, the tree wins.

## 1. Current baseline (frozen)

- Branch `main`, latest commit at audit time `e268341 test(ai): P21.7 policy
  switching and final acceptance`.
- Worktree clean (`git status --short` empty).
- Build/test: Debug preset, `ctest --preset debug` is **11/11 pass**. The single
  slow entry is `sim_runtime` (~294 s); `ai_unit` is ~10 ms. Development
  iteration must use `-L unit` / `-R ai_unit`, never the runtime harness for
  every edit, and never a full match (`AGENT.md`).
- Core-only (`BUILD_TESTING=OFF`) builds `libsimcore.so`; the A17 digest noted
  by P21 is `5bef607ddd567a09` and is unchanged by this audit.

### Current AI <-> Player contract (P21, frozen)

```
include/football/ai/
  ai_config.hpp       AIConfig{ initial_tactics[2], PlayerPolicyKind player_policy=Default }
  tactical_board.hpp  TacticalBoard{ side, width=0.75, depth=0.55, vector<PlayerDirective> }
                      PlayerDirective{ PlayerId, PlannedPlayerRole, optional<Vector3> formation_position, optional<PlayerId> marking_target }
                      MakeTacticalBoard(TeamSetup, TeamSide, Pitch)
  player_policy.hpp   IPlayerPolicy::Update(const WorldState&, span<const TacticalBoard,2>, PlayerControlSet&)
                      PlayerPolicyKind { Default, Eliza }
  default_ai.hpp      public baseline DefaultAI owning TacticalBoard[2]
src/ai/
  ai_engine.{hpp,cpp} owns TacticalBoard[2] + one IPlayerPolicy
  default_policy.*    adapter over shared ComputeDefaultControls
  default_decisions.* the shared Default decision body
  eliza/*             ElizaPolicy + role/keeper/on-ball/support modules
```

Runtime order (`src/env.cpp`): `Observe() -> AIEngine::Update(world, controls)
-> Simulation::Step(controls)`, exactly one `Update` per 100 Hz tick.

Key facts this migration must not break:

- `default_decisions.cpp` and `eliza/off_ball.cpp` scale the stored
  `formation_position` by `board.depth / 0.55f` and `board.width / 0.75f`. Any
  change to the legacy fields changes the baseline control stream.
- `WorldState` is a value snapshot in one pitch frame (home defends -x);
  `WorldTeamState.defending_direction` is the observed per-team direction and
  does not flip when ends change. It is the only coordinate truth the AI has.
- The AI layer includes nothing from `runtime/`, `player/`, `referee/`,
  `observation/`, `event/`, `testing/` or `sim/`; it depends only on public
  value contracts (`football::ai_contracts` -> foundation/pitch/sim_contracts).

## 2. Ownership map (target, unchanged in spirit)

| state | owner | mutation entry | readers |
| --- | --- | --- | --- |
| pitch/players/ball/rules/score | Simulation | existing `Step`, Rules | WorldState |
| persistent coach plan | AIEngine -> CoachRuntime | validated command executor | PlayerAI |
| transient mental / shout effects | CoachRuntime | shout resolver | PlayerAI (read-only) |
| attack plan / defensive assignments / decision memory | PlayerPolicy (private) | its own `Update` | that implementation |
| actual motion / action queues | Player/Simulation | existing `PlayerControlSet` | simulation |
| agent session, model text, network | future CoachAgent | agent loop | Coach only |

One persistent board per side plus a monotonic `tactical_revision` is the only
authority. `EffectiveTactics` is a derived interpretation, never a second board.

## 3. Concept sources and licence constraints

- **FM26 dual-phase tactics** (public feature page): in-possession and
  out-of-possession formations with phase-specific roles, and the removal of the
  legacy Attack/Support/Defend duty system. We take the *two-phase shape* idea
  and give each slot an in/out-of-possession anchor and role; we do not copy UI,
  names or internal algorithms.
- **FM touchline shouts** (FM24 and earlier): short-lived psychological messages
  to specific players. FM26 is not assumed to ship them; the design is an
  independent reference to the historical feature. Shouts are stored separately
  from the persistent board and only feed bounded mental effects.
- **Open Football** (`ZOXEXIVO/open-football`, Apache-2.0): team attack/defence
  plans, box slots, rest defence, exclusive marking and stable assignment. We
  may port ideas under Apache-2.0 compliance with source attribution.
- **Player12** (`FlavioMili/Player12`, GPL-3.0): passing/shot candidate scoring
  and match-shout concepts. **No code may be copied, translated or linked.**
  Only publicly described tactical concepts are re-implemented clean-room.
- **Eliza** (in-tree, the current heuristic) is the baseline we keep as a
  selectable policy; its force-field positioning is a legitimate positioning
  candidate for the new unified policy.

## 4. Protocol gaps and deferred items

- `IPlayerPolicy::Update` cannot see transient shout effects. TACT-6b must
  introduce a versioned `PlayerAIContext` / `IPlayerPolicyV2` (or equivalent
  explicit upgrade) with a `LegacyPlayerPolicyAdapter`; shouts must never be
  smuggled into `TacticalBoard` or `WorldState`.
- No substitute/red-card tool exists and none is added here; a future
  `substitute_player` would be validated by Simulation/Rules, not the board.
- No network/LLM/provider/threadpool is introduced; coach tools are
  deterministic C++ domain commands with a schema projection for the future.
- `unsupported`/`deferred` tactical fields must be documented, never faked.

## 5. Per-phase gates

| phase | deliverable | gate |
| --- | --- | --- |
| TACT-1 | `tactical_plan.hpp` v2 schema + `advanced` board field + `MakeTacticalPlanV2` | legacy Default control stream field-identical; schema validation rejects bad values |
| TACT-2 | pure `TacticalInterpreter` + `EffectiveTactics` | HOME/AWAY mirror, IP/OOP anchors differ, finite values, half consistent |
| TACT-3 | Eliza reads v2 via an explicit switch | strong/weak tactic scenarios differ; restarts/GK/actions not regressed |
| TACT-4 | `HeuristicPlayerPolicy` (new `PlayerPolicyKind`) | 22 controls assigned, no clustering, tactics responsive, deterministic |
| TACT-5 | coach commands + tool registry + transactional executor | apply at exact next tick, revision monotonic, invalid command has no side effect |
| TACT-6a | shout lifecycle (IssueShout/cooldown/expiry/log) | board unchanged, duplicate/expired/illegal targets handled |
| TACT-6b | versioned policy context exposes mental effects | decision tendency changes, base attributes untouched |
| TACT-7 | CoachContext collector + scheduler + scripted loop | stale revision/tick rejected, no opponent private read, no network |
| TACT-8 | contract doc + sample tactics + acceptance matrix | bounded suites, core-only, header smoke, clean commits |

## 6. Risk list

1. **Baseline drift.** Any edit to `tactical_board.cpp`, `default_decisions.cpp`
   or `eliza/*` scaling can move the Default stream. Mitigation: keep legacy
   fields read-only in legacy mode, add the new path behind `advanced`.
2. **Two boards.** A coach runtime that copies the `TacticalBoard` would create
   a second authority. Mitigation: `AIEngine` remains the sole owner; the
   executor mutates the one board in place and bumps a revision.
3. **Coordinate double-conversion.** `BuildPlayerCommands` converts exactly
   once. The interpreter only produces *intent* targets in pitch metres; it
   never writes a `Player` position.
4. **Shout leakage.** Storing shouts on the board would make them persistent.
   Mitigation: separate `ActiveShout` storage in the coach runtime.
5. **Interface churn.** Adding mental effects to `IPlayerPolicy` must be a
   deliberate, versioned break with a legacy adapter, not a silent signature
   change.
6. **GPL contamination.** No Player12 source, translation or link. Reviewers
   check each heuristic module against the public concept only.
