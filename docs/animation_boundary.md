# Animation resource/runtime boundary

## Ownership

`football::animation_base` owns canonical catalog import and immutable resources.
Private `src/animation/import/` code parses GLBs and metadata, mirrors/generates
clips, derives support, repairs gaits and measures motion quality. Its
`ImportedAnimationClip` workspace is discarded when publishing `AnimationClip`.
Only root/pose tracks, contacts, metadata and scalar resource facts survive.

`football::animation` owns paired reference sampling, pose sampling/blending,
transitions, entry-frame pose distance and pure geometry. It has no Player,
Runtime, Team or Ball implementation dependency. CMake checks this include
boundary; whole-archive linking and standalone public-header compilation test
its exported interface without actors or assets.

Player Execution owns action-specific touch tolerance/scoring, capability
mapping and turning preferences (`player_touch_geometry.hpp`,
`player_movement_math.hpp`). A pure function is not necessarily an Animation
function. Selection order, action timing and motion constraints are unchanged.

## Root sampling audit

All times below are **clip frames**, not seconds: one frame is 10 ms at native
playback rate. Canonical root tracks are XY; pelvis Z stays in the pose.

| Interface | Current role | Boundary semantics |
| --- | --- | --- |
| `SampleAnimationStep(clip, from, to)` | Production root-driven execution | Paired root/pose, unwrapped multi-cycle time for loops; clamped non-loops; reversed interval holds at start |
| `SampleRootPosition(roots, time)` | Shared low-level interpolation | Origin-relative, fractional, clamped; no pose or loop policy |
| `LocomotionReferenceDelta(roots, from, to)` | Player compatibility playback and diagnostics | Times within one cycle; `to < from` means one wrap; preserves seam addition order |
| `ExtractMotionClip` / `SampleDisplacement` | Tested discrete reference contract; no production callers | Accumulated XY deltas; always clamped, no loop flag; reversed intervals produce reverse displacement; no pose required |

The production paired sampler and compatibility playback now share the same
root interpolation implementation. The compatibility wrapper is deliberately
not replaced by the paired sampler: a wrapped end is not unwrapped time, and
its two-segment seam addition has a different floating-point evaluation order.

MotionClip is retained, not treated as an equivalent runtime entry point.
Summing extracted deltas and subtracting source positions are mathematically
close for forward non-looping intervals but need not be bitwise identical.
Loop accumulation, reversed intervals and resources without poses also differ.
`root_sampling_contract_test.cpp` and Player compatibility tests lock down these
distinctions, including nonzero origins, fractional/end/out-of-range sampling,
multi-cycle travel, empty/single-root resources and pose-only sampling.
New root-driven execution code should use `SampleAnimationStep`, not MotionClip
or the compatibility wrapper.

## Schema compatibility and remaining policy coupling

Catalog action tags and joint indices retain their numeric values, including
historical enum slots. Renumbering them is an asset-schema change, not cleanup.
Candidate `DataSet` belongs to the private Player filter; unused `ClampVelocity`
is removed. Import quality thresholds are private; runtime reads their measured
verdicts rather than re-running support analysis.

`kGaitRepairMaxPlaybackRate = 2.2` still preserves the existing repaired-metadata
clamp. Removing this coupling requires defining and validating an asset playback
range separately from Player's executable range. It is intentionally not changed
by the structural refactor: doing so now would change loaded metadata and
candidate selection. Likewise, no movement model, candidate sort or Tick phase
has been redesigned here.
