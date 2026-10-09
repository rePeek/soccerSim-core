# Snapshot recording and historical data

Snapshot is an owning, read-only record of authoritative Ball/Player movement
and animation, not WorldState, a checkpoint, or a rules/score cache. The sole
producer is Simulation. No new consumer replaces MentalImage or changes physics,
contact facts, rule decisions, AI policy or RNG draws.

## Sampling and lifecycle

- `MatchOptions::snapshot_capacity` defaults to 60,000 (ten minutes at 100 Hz).
  Capacity is positive and configured before Init; each slot is preallocated.
- Init captures executed step zero. Every successfully executed Step commits one
  record, including ceremonial and terminal early returns; Finished is a no-op.
- Stamps separate strictly increasing executed `step_index`, nondecreasing
  `timeline_tick` (the clock value **after** Step), and reset `generation`.
  A terminal whistle may repeat a tick. Diagnostic AdvanceTime creates no frames.
- ResetSituation keeps earlier history; the next committed sample has the new
  generation. Stop releases history/actor borrows; a new Init starts a new match.
- Slots are fixed home-then-away runtime actor order, not processing order or
  remapped PlayerIds. Inactive actors retain slots, with animation -1/frame zero.
  The current Team creates formation actors only; omitted profiles are not actors.
- Pitch transforms match `ToHomePitchFrame`/WorldState in both halves/orders;
  angular velocity uses the same proper rotation (no reflection).
- Ball data comes from `Ball::state()`. WorldState's legacy `Predict(0)` cache can
  lag actual position. Snapshot does not refresh that cache to force equality.
- All live history access stays on the simulation thread and in memory. Borrowed
  pointers expire on overwrite/Clear/Reset/Stop; background code needs copies.

## Live historical consumers

SnapshotHistory::ForEachStep visits retained executed-step ranges synchronously
without copying or allocating, in chronological order across ring wrap. It returns
the actual visited count; overwritten/missing steps are never fabricated. Moves
leave the source queryably empty; Reset initializes it for reuse.

EventRecognizer keeps existing contact-fact start/resolve/timeout/goal semantics.
During real Step execution, EventView carries an optional executing sample step.
Resolved actions queue observation windows; only after CaptureSnapshot commits the
whole step does PublishTrajectories query the same history and publish owning
EventTrajectory values. They include event identity, generation, requested step
range, sample count, first/last ball positions, path length and peak speed.
Missing retention, generation mismatch, step gaps and timeline jumps mark analysis
incomplete; distance never crosses these discontinuities. Diagnostic facts outside
Step have no executing sample annotation and never pretend to have motion evidence.
These whole-step summaries are not precise contact positions or referee verdicts.

GameEnv::CopySnapshotWindow returns an owning oldest-to-newest memory window and
leaves output unchanged when insufficient samples are retained. EventTrajectories
returns owning summaries published at the latest step. Stopped telemetry calls throw.

GraphBuilder::BuildLatest(history, metadata, count) uses that same in-memory window;
an insufficient window returns nullopt, not a disk fallback. Build(window, metadata)
also accepts offline archive ranges as values. Nodes preserve a ball slot followed
by fixed player slots for every frame, with physical/animation features and inactive
masks. Directed spatial edges connect active nodes within a configurable radius
(default 15 m); forward temporal edges connect the same active slot only across
consecutive executed steps AND consecutive timeline ticks in one generation.
No edge crosses reset, missing-step/tick or duplicate-tick boundaries. Nodes/edges
and stamps are owning values. Invalid/duplicate IDs, count mismatches, non-finite
vectors/radius and out-of-order windows fail explicitly. This is a data interface
for future Temporal GNN consumers, not a trained model or new AI policy.

Referee continues to judge exact immutable contact evidence. Historical geometry
can query Simulation::Snapshots() by exact tick/generation; it cannot trigger
archive I/O. No animation/resource/rules cache is duplicated in Snapshot.

## Recording a match

```sh
build/release/football_app --snapshot=match.snap
build/release/football_app --half-duration-ms=1800 \
  --snapshot-capacity=100 --snapshot=short.snap
```

Disk recording is opt-in and app-owned (`football::recording`); libgame and sim
never link the writer thread or archive I/O. GameEnv::CopyLatestSnapshot copies
an already committed sample into reusable caller storage; it never resamples.
The app appends initial step zero and every subsequent Step, then explicitly
closes the archive before reporting success. Capacity affects memory retention,
not how much of the match is archived.

SnapshotArchive preallocates a bounded queue (default 1,000 records) and copies
Append inputs. The default Block policy applies backpressure when full; Fail
aborts/report errors and leaves no normal-completion footer. Disk errors are
re-thrown from Append/Flush/Close and the worker is joined on teardown. Call
Close explicitly: a destructor cannot report errors to its caller. Flush drains
and materializes a partial chunk and flushes the stream; it is not fsync or a
power-loss durability guarantee. Completion means a normally closed recording
session, not proof that the referee finished a full match.

## Offline reading

```cpp
using namespace football::app::recording;
SnapshotArchiveReader reader;
reader.Open("match.snap", expected_animation_hash);
auto frames = reader.ReadRange(100, 249); // inclusive executed steps
```

The reader checks version/rate/frame/metadata/hash and the checksummed index and
footer. Range reads binary-search the index and read only intersecting chunks;
chunk CRC, count, stamp order and index correspondence are checked before return.
No live Referee/Event/graph query invokes this reader.

`ArchiveReadMode::RecoverPrefix` explicitly opts into scanning and exposes only
whole checksum-valid chunks before the first damaged/truncated chunk. Missing
frames are not interpolated. Complete() is false when recovered; incompatible
headers/hashes still fail. A complete-mode index open does not scan irrelevant
chunk payloads; corrupt data fails when its range is read. Recovery mode validates
all chunks, including when an index/footer survived.

## Version 1 binary format

All integers are unsigned little-endian except animation IDs (signed 32-bit
bit pattern). Floats are IEEE-754 binary32 with their exact bit representation.
No C++ structs/padding/vector pointers are serialized. All block payloads use
CRC-32/ISO-HDLC (polynomial 0xEDB88320, initial/final XOR 0xFFFFFFFF).

```
magic[8] = "SOCCSNP1"
SHDR block
CHNK block ...
INDX block
END! block (32 bytes total)
```

Every block: `tag:u32, payload_bytes:u64, payload_crc32:u32, payload`.
Tags: SHDR=0x52444853, CHNK=0x4B4E4843, INDX=0x58444E49, END!=0x21444E45.
Safety limits: block <=64 MiB and slots <=100,000; chunk/queue capacities must be
positive and a configured chunk must fit the block limit. There is no compression.

- **SHDR payload**: version:u32=1, tick_rate:u32=100, coordinate_frame:u32=1,
  animation_hash:u64; 11 floats (pitch length, width, quadratic resistance,
  ground deceleration, grass height, line half-width, goal half-width, goal height,
  goal depth, penalty-area depth, penalty-mark distance); slot_count:u32;
  PlayerId:u32 for each slot. Hash is FNV-1a-64 over exact loaded .simanim bytes,
  offset basis 14695981039346656037, prime 1099511628211 (not cryptographic).
- **CHNK payload**: count:u32, first_step:u64, last_step:u64, records.
  Default 100 records/chunk; Flush or Close may produce a smaller chunk.
- **Record**: step:u64, timeline_tick:u64, generation:u64; Ball's position,
  velocity, angular velocity (three xyz float vectors); one Player per slot:
  position, velocity, facing, body_facing (four xyz vectors), animation_id:i32,
  frame:u32, active:u8, possession:u8. Booleans must be 0 or 1.
  Wire size = `60 + 58 * player_slots` bytes, independent of C++ alignment.
- **INDX payload**: entry_count:u64; entries of first_step:u64, last_step:u64,
  file_offset:u64, total_block_bytes:u64, frame_count:u32.
- **END! payload**: index_offset:u64, total_records:u64. Partial footer/index
  cannot mark a prefix as complete. Append allows step gaps and duplicate ticks
  but rejects non-increasing steps, tick regression and generation regression.

Snapshot archive format upgrades must version schema/frame conventions and
retain animation resource provenance, rather than interpreting old IDs using a
new library silently.
