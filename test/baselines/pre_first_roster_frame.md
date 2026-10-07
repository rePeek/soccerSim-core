# Before correcting first-roster execution frames

Runtime `85e4bdf` / measurement-only `51cb1f6`. This is an intentional spatial
correctness change, not a unit migration or balance/clock adjustment.

The canonical between-tick ball shares the first processing roster's frame.
Match previously mirrored that ball again when the first roster was away, while
leaving away actors unchanged. Both native action execution and possession rollout
then used a ghost target; collisions/goals also compared different frames.
The new scopes mirror the OTHER roster, not the already aligned ball. The shared
all-actor turn between first/second processing is restored with an unconditional
ball turn at exit. Referee uses the same adapter as observations; the special
reverse compensation helper is deleted. Normal processing operations are identical.

## Independent causal regression before the correction

`native reachability targets the nearby real ball in either actor-processing frame`
places a stationary ball at physical (23,7,.11), both selected actors within 1.2 m,
clears action/perception state, then executes ten ticks to cover all refresh phases.
No DefaultAI balance intervention occurs. Both normal halves pass. Reverse first
half, home actor reports 3000 ms (full prediction-horizon absence), despite the
correct observed nearby distance. Captured pre-fix failure: 1897 assertions, one
failure, `3000 < 3000`. The mirror target is ~49 m away and unreachable in that
horizon. The corrected test covers both actors, orders and halves.

The long regression records accepted intentional kicked contacts during native
open play, excluding kickoff/restart contacts, over two 5000-tick halves, and replays
the complete contact sequence, clocks, scores and RNG. It has no desired score or
effective-duration target. Historical full reverse runs had no meaningful open-play
progress: seeds42/43/44 all 0–0, two ordinary throw-ins, effective539518/539518/539505
of540000 regulation ticks. Full table is in tools/restart-metrics.md.

## Historical identity fingerprints

```
{false,false,16144338831632478706,11101384966440775508}
{false,true, 1278980483242646393, 2155356947635217068}
{true,false, 5415142161785239299, 12038476573473573858}
{true,true,  8110407433742795352, 14851915034266500703}
```

Only unequal/reverse changes (full rosters are still before actual kickoff at
these300/600-call checkpoints): `16362423676506032975,10000127481615488157`.
The normal core rows and both normal identity rows remain unchanged. No golden
was updated to mask the failing spatial test; causal geometry was pinned first.
Correct reverse execution changes the incidental signed-zero bits at initial
centre-ball observation from +0 to -0; the owned-value geometry test now uses
numerical equality for that centre, without production normalization or float
expression-order changes. Other bit-level observation/round-trip tests remain.

No restart bounds, AI coefficients, baked data, RNG operations, SI integration
expressions or clocks were changed. Full before/after matrix and three-mode
validation are documented separately. No realism calibration is claimed.
