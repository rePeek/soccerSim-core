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

TEAM-1 (1c840cf): pure Team values, conversion, validation, shared AI/runtime
slot identity and dedicated team_data_unit. Debug CTest 12/12 (168.23 s).
TEAM-2 (3a90a16): Simulation teams_ replaces team_setups_ entirely. Team definitions
reach final storage before actor creation; no live Team mutation API. Formation
adapters share the unchanged algorithm without copying definitions. Added
address-identity, caller-input destruction, reversed-order, Stop/re-Init and
post-actor allocation-failure recovery tests. Debug CTest 12/12. The existing
bounded 22-actor test reports the same digest as the clean baseline:
5bef607ddd567a09 (22 actors, 18 switches). No extra RNG calls were added.

TEAM-3 (536d9c3): explicit lineup creates actors in definition declaration order, assigning
each starter its formation slot and leaving reserves inactive. Explicit shapes
are slot-indexed (up to 11 entries), independent of definition count; absent
entries use existing FormationEntry defaults. Simulation requires at least one
starter per side; pure Team data can still express an entirely vacant lineup.
Default unspecified-lineup construction/activation/RNG order is unchanged.
Runtime owns active_lineups_; send-off vacates only the departing identity.
Observe projects availability; AI uses stable bindings, with legacy fallback
only for synthetic WorldState without metadata. Leadership/taker queries are
pure, deterministic and not wired into referee execution. Candidate-order fallback
is explicit, never PlayerId ranking. No substitution execution command is added.
Debug CTest 12/12 passed; ownership tests and AI vacancy/replacement tests pass.
The existing bounded 22-actor digest remains 5bef607ddd567a09.

TEAM-4: FormationPresetLibrary now has standalone validation, const ID lookup and
one pure planar coordinate converter for team-relative and ball-relative anchors.
TeamRelative x in [0,1], y in [-1,1] exactly match TacticalAnchor u/v, NOT the
historical InitialFormation convention. BallRelative x/y are finite metre offsets,
positive towards opponent goal / team-left. Defending direction is explicit in
the caller's frame; TeamSide never implies it. Lateral mirror precedes direction
rotation. Overflow is rejected; templates and the physical ball are never changed.
No clamp or rule legalization is hidden in the converter. An off-pitch candidate
can be valid template data; a future planner must legalize actual assignments.

Final Debug CTest: 12/12 passed (169.77 s, sim_runtime 166.96 s). Original 11
entries remain green. team_data_unit: 11 cases / 138 assertions; dedicated
ownership tests: 5 cases / 120 assertions. Existing 22-actor bounded digest still
5bef607ddd567a09. No full match, app execution, Release build or removed golden
tool was used. git diff --check passed.

## Deliberate limits and integration gates
- Runtime has one Team configuration per side; GameEnv's const external input
  declarations remain solely for restart/bootstrap compatibility. AIEngine still
  bootstraps TacticalBoard from TeamSetup, without a writable Team copy.
- Slots no longer compress after send-off when authoritative metadata is present.
  This is the intentional AI identity correction, not a change to rule or physics
  behavior. Other default construction/RNG paths are retained.
- Default short formations still omit actors for extra definitions; changing that
  would alter legacy creation RNG/scheduler order. Explicit lineups construct all
  declared players, with only bound starters activated.
- A vacancy/replacement data operation exists; full substitution execution and
  legal eligibility production are future Runtime responsibilities.
- Formation conversion produces candidates, not referee-approved placements.
  Helpers currently belong to the existing sim target; AI consumes only value
  contracts and must not gain a concrete sim dependency when integration lands.
- RFE-2 Runtime RestartContext ownership is absent on this base. TEAM-5/6, dynamic
  Coach commands/versioning, preset selection by coach and on-field execution are
  intentionally deferred. Referee placement, touch authorization and RNG are untouched.
- Address/lifecycle and post-actor failure recovery are directly tested; ASan/UBSan
  was not run. Future Runtime code must continue to forbid live Team movement or
  player-definition vector reallocation.

## TEAM-5 — Runtime Team Entity Ownership (supersedes static Team ownership)

### TEAM-5.0 audit
Base: 89b19e186906b5a0358ccd4c47f22a8743db3c00; initial worktree clean.
The earlier TEAM-5 restart-integration label is superseded: restart integration
is now TEAM-6; substitution execution TEAM-7. ball_netting is excluded.

Current owners: Simulation teams_[2] holds definitions; rosters_[2] holds actors;
active_lineups_, team_frames_, team_states_ hold relationship state. Player borrows
model_ and derives body_profile_ once. Target: Simulation unique_ptr<Team>[2];
Team privately owns Roster, config (no definition array), lineup/frame/match state;
Player owns const PlayerDefinition. No Player -> Team dependency is introduced.

Reference audit: simulation.cpp (initialization, queries, mirrors, possession,
rules composition, snapshots, teardown); simulation.hpp (inline designated-player
write and owner declarations); simulation_prepared_tick.cpp (actor ordering and
possession); simulation_baked_body.cpp / simulation_body_shadow.cpp (actors);
testing/simulation_access.hpp (test borrows). TeamView, team_facts and existing
roster algorithms remain borrowed projections / explicit algorithms. Pure Team
preparation/validation and formation tests must move off the public Team struct.
AI and GameEnv do not retain runtime entities; their public APIs remain unchanged.

Ordering to preserve: validate IDs/config before RNG; appearance seed-0 home then
away across ALL declarations; episode seed; construct first then second processing
roster, phases crossing the roster boundary; snapshot/index order home then away.
Default short formation omits extra actors, explicit lineup creates all actors.
Tick phase order, synchronous fact publication, accepted touches and rule commands
are unchanged. Stop exits actors home/away before releasing their owners; all
borrowed indexes/diagnostics must be cleared before Team destruction. Failed Init
must take the same cleanup route, including partially constructed rosters.

Validation: bounded CTest only, no app/full match. Preserve existing Debug digest
5bef607ddd567a09; run Debug at every stage, then requested Release and bounded
ASan/UBSan ownership/failure tests. Do not revise the digest to mask a difference.
