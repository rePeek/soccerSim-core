# P7b pre-promotion baseline (default OFF)

Recorded BEFORE any default production switch, per the P7b discipline. This is
a migration reference, not a new Golden: do not overwrite it casually.

- Baseline source: `1e4f547` plus the P4f-2c-a/P4f-3-a/P6c-a/P6c-b commits on
  top (see `git log`). Defaults still OFF; assets/Golden untouched.
- Regeneration:
  `nix develop --command build/debug/football_regression --print-baseline`
- Off-default regression fingerprint (diagnostic samples at steps 1/100/500/1000):

```text
{1, UINT64_C(4093443199407095679), UINT64_C(15853908816633971195)},
{100, UINT64_C(6103418754402414502), UINT64_C(15665604445674600024)},
{500, UINT64_C(16326727902995697086), UINT64_C(5319442467002808952)},
{1000, UINT64_C(6505713038984341129), UINT64_C(5637067788866395863)},
```

- `--animation-ab` and `--animation-ab-lifecycle` remain byte-identical to the
  pre-P4f baseline; the unified-path switches are OFF so none of this work
  perturbs the default trajectory.
- After P7a flips the default, re-derive the same rows for the ON path and
  record the intentional deltas (ball spin/trajectory, body-contact counts,
  RuleTouch classification, RNG consumption, performance) alongside the
  reviewed new Golden, never by copying a failing-test hash.