# WorldState schema before final input migration

Source: `c04746f` on `feat/anim-base`. The default value policy is unchanged by
removing legacy Human/controller ownership. Only the snapshot schema/hash changes:
`externally_controlled`, timed run/pressure/rush requests and marking disappear;
`reset_sequence` projects actual Match::ResetSituation discontinuities instead.

| Tick | Previous WorldState hash | New WorldState hash | Unchanged sim digest (motion/actions/rules/RNG) |
| --- | --- | --- | --- |
| 1 | 16938073673177634806 | 5371189051509720323 | 350760935552669674 |
| 100 | 15465377132528825825 | 1291515177572478404 | 13725550824419574755 |
| 500 | 14448294930850262157 | 1650659871221397832 | 7040189435179413637 |
| 1000 | 480541022030217475 | 1772856281264394018 | 10968404906479542722 |

These are 100 Hz raw core snapshots, not the retired GRF observation cadence.
No simulation/RNG, scheduling/send-off, restart-placement or animation A/B goldens
were regenerated. All control sources now share the same source-blind execution
mechanics. The app GRF decoder preserves action wire numbers, but deliberately
uses one-shot kicks/sliding/switch and explicit power instead of the removed
Human animation planner/gauge. That input-policy change is not a claim of
bit-exact compatibility with the old Human path.

Historical note: the app GRF decoder described above was subsequently retired
entirely. Simulation replay now uses explicit PlayerControlSet tapes; the historical
hashes and numerical baselines in this table remain unchanged.
