# P4f-2b contact-region checkpoint

This is an experimental coverage checkpoint, not P7 promotion. No Ball API,
CCD kernel, normal Step count, actor execution count or RNG compensation changed.
Defaults remain OFF; assets and Golden are untouched.

## Geometry and identity

`player_baked_contact_regions.hpp` adds nine Capsule/Sphere regions: chest,
two boots, two upper arms, two forearms and two palms. The six original IDs
remain unchanged; new IDs occupy the checked `0x40000000..0x7fffffff` band,
with fixed roster slots independent of PlayerId, traversal or mirroring.

The original torso primitive now follows the pelvis body's rigid transform;
chest follows middle separately. A body-to-neck axis cannot enclose rigid
pelvis/trunk meshes reliably when the spine bends. All 15 physical shapes are
constructed from the same committed realized skeleton. The provider reserves
and reuses its tape, maps all extra IDs to owners, and shares semantic episode
keys with adjacent existing surfaces. No missing boot/arm collision is hidden
by a filter or diagnostic gate.

Offline local mesh maximum distances to chosen axes, with chosen radii:

| Mesh | Measured maximum (m) | Radius (m) |
| --- | ---: | ---: |
| pelvis | 0.198895 | 0.200 |
| trunk | 0.232297 | 0.233 |
| upperarm | 0.0712452 | 0.072 |
| lowerarm | 0.0525438 | 0.053 |
| foot | 0.0701925 | 0.071 |

There is no separate hand bone or hand mesh in this model. Palm spheres are
explicit distal lowerarm regions (local z <= -0.24 m), not invented joints.
Distal vertex maximum distance from `(0,-0.005,-0.30)` is 0.0590219 m; sphere
radius is 0.065 m. Hair/cosmetics remain nonphysical. No runtime mesh loading.

Vertex enclosure tests now cover pelvis, trunk, boots, arms and distal palms
in addition to head/legs, across real Movement/Sliding/Trip poses and two scales.
ID boundary, primary-ID separation and mirrored motion tests cover new regions.

## Transport error and runtime replay

The dense 1/16-Tick audit includes all 15 shapes, not only the original six.
28,288 adjacent intervals across 1,016 Movement, 12 Sliding and 38 Trip clips:
raw maximum primitive endpoint deviation versus local quaternion motion
0.0112886 m; realized maximum 0.0186184 m. Worst realized interval is
sliding/sprint/000_ballside.anim, frame 8. This updates the six-shape 14.069 mm
measurement; it is not an automatically accepted production error threshold.

The actual provider replay checks all 15 start/end shapes against published
realized pose data in both processing orders, with exact state/orientation,
actor-frame and RNG equality under active Shadow ON/OFF. The real tackle
scenario is requested while play is live, rather than at a fixed time that may
fall in restart positioning. Earlier fixed-window reverse replay produced no
Sliding frames after the changed physical outcome; the assertion was retained,
and scenario scheduling corrected. Constructed head-strike evidence remains
explicitly labelled, not counted as spontaneous native coverage.

## Experimental physical kick / restart bridge

Expanded boot collision exposed a timing boundary: genuine boot impact can
precede the animation's scheduled touch marker. Such contact must not be hidden
or followed by another artificial impulse. A mapped active player's approaching
positive boot impact during an authorized Shot/ShortPass/LongPass/HighPass action
is provisionally classified as deliberate kicked play. Other live body impacts
remain accidental deflections. Before live play, acceptance additionally requires
the Ready restart's designated taker and set-piece authorization.

`AcceptedTouch::physical_kick` carries that reviewed producer decision through
the existing sink to BallTouchFacts. Referee may release the Ready taker on this
physical kick, independently of the scheduled marker; old producers default
false and keep their scheduled-contact behavior. The original ColliderId/contact
and RuleTouch source remain BodyCCD. This is NOT complete P6c acceptance: exact
intent windows, early/back-foot contacts, unauthorized/non-taker cases and broader
restart/offside/keeper consequences still need dedicated semantic scenarios.

## Validation and stop boundary

Focused Release: 33,327,901 assertions / four cases. Full Release CTest 42/42
(405.89 s), Debug 41/41 excluding full-regulation CLI (960.71 s), core build
passed. Default-OFF regression matches `/tmp/ball-7e71690-baseline.txt` exactly.
Logs: `/tmp/p4f2b-coverage-{focused.log,release-ctest.log,debug-ctest.log,core.log}`
and `/tmp/p4f2b-coverage-baseline.txt`. CTest duration is not Tick latency.
These already-completed runs predate the maintainer's Debug/core-only development
instruction; subsequent development must follow the updated AGENT.md.

At the maintainer's request, stop after committing this checkpoint. The entry
smoothing 0.215 m/Tick catch-up assessment, actual per-action impact trajectories,
P4f-3 point/technique/spin/flight calibration, comprehensive P6c and full ON/ON
regulation acceptance, reviewed Golden/default switch and Legacy deletion remain
open. Do not label this commit P4f completion or P7, and do not start Ball Model
Bake as part of this checkpoint.
