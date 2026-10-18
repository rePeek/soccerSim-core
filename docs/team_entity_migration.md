# TEAM — Match Team Entity

## TEAM-0 audit (base 819f087ee43abfbb572de98f086f0d96f1917b4a)

Working tree initially clean; no uncommitted RFE work.

| Data / old field | Authority / migration |
|---|---|
| TeamSetup name, players | compatibility input -> Simulation-owned Team name, players |
| TeamSetup formation | Team initial_formation; preserve legacy public-start coordinate convention |
| TeamSetup tactical_formation | Team tactical_formation; normalized legacy shape, unchanged BuildFormation algorithm |
| TeamSetup tactics | initialization-only compatibility preferences; TacticalBoard remains current AI authority |
| lineup / leadership / taker orders / presets | Team configuration; no actor pointers |
| actual Player objects | Roster, unchanged |
| orientation / possession / match facts | Runtime TeamFrame / TeamMatchState, unchanged |
| live slot binding | Runtime ActiveLineupState, projected read-only into WorldState |
| current tactical intent | AIEngine-owned TacticalBoard, unchanged |
| restart state / placement | presently RefereeState / PositionRestartPlayers; deferred to RFE |

GameEnv retains const input declarations for Start after Stop and passes them to
Simulation and DefaultAI. AIEngine builds boards from these inputs, not a mutable
copy of Team. Simulation currently copies TeamSetup into team_setups_ before
creating Player objects that borrow const PlayerDefinition&. Definitions must
remain at their final addresses until all actors are destroyed. Stop and failed
Init must clear actors before replacement of configuration. Simulation is not movable.

Init validates identities before RNG; appearance uses a separate seed-0 stream in
home/away order (including omitted profiles). Simulation RNG is seeded afterward.
Player construction follows processing order; schedule phase crosses roster boundary.
BuildFormation count is initial formation size, else tactical size, else player
count. It performs ordered role blending and separation, then applies start poses.
Initial y uses FORMATION_Y_SCALE; tactical y does not. Default activation is first
min(11, constructed count). Extra declared definitions may have no legacy actor.
This omission is preserved for unspecified lineups rather than changing RNG draws.

MakeTacticalBoard / MakeTacticalBoardV2 consume TeamSetup directly; their algorithms
must not change. TacticalPlanV2 slots are independent of PlayerId. Existing
DefaultSlotAssignment compresses active observation order after a dismissal: migrate
to explicit runtime slot projection, retaining observation-order fallback only for
synthetic/legacy WorldState inputs without authoritative bindings.

SendOffEvents synchronously calls HandlePlayerSentOff; no product substitution
command exists. A slot replacement data operation can be tested independently;
adding a full substitution execution mechanism is not in this migration.

There is no RestartPlanner/PlanRestart or runtime RestartContext in this checkout.
Referee owns restart buffer and PositionRestartPlayers selects/places players using
legacy order, distances and RNG. Ruling/RuleCommandSink exist, but RFE-2 ownership
has not landed. TEAM-5/6 must not modify these paths. Empty new orders/presets must
remain inert even after data support is introduced.

Validation uses Debug CTest only, never football_sim/full-match or removed goldens.
Baseline results and phase results are recorded below as they become available.

TEAM-0 baseline: clean detached worktree at 819f087, nix Debug preset build,
CTest 11/11 passed (178.40 s; sim_runtime 175.45 s). Build cache is shared;
the baseline source tree is isolated from implementation edits.
