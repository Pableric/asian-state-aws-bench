# Asian Option Engine Agent Notes

## Baseline

This project starts from European engine commit `4aa404d`, the corrected
ordered-D1 implementation. Preserve its correctness and native performance as
the initial regression baseline.

Before changing Gaussian, Sobol, AVX-512, or pricing code, read `TODO.md` and
`EUROPEAN_BASE_AGENTS.md` completely.

## Objective

Evolve the current European ordered-D1 engine into a 32-fixing arithmetic
Asian engine. The immediate task is caller-selected D2/other-dimension
construction aligned with the existing D1 point identities. Later work will
replace the terminal European payoff with the Asian Markov state `(S,Q)`.

## Guardrails

- Keep the current two-ZMM/32-path packet shape.
- Preserve point identity exactly across dimension reordering.
- Do rank/position discovery during preparation, not inside the packet loop.
- Compare precomputed load/permute against direct in-register construction on
  native AVX-512 hardware before choosing.
- Avoid materializing intermediate Sobol values unless it is measured faster.
- Do not replace the hot assembly with scalar code or a generic per-lane loop.
- Keep Gaussian/pricing implementation private when delegating isolated work;
  share only a narrow register and data-layout contract.
- Treat `research/previous_asian/` as historical reference, not production
  source. Its experimental direction-number constructions are not validated
  replacements for OpenEvolve.
- Preserve the archived original directory. Do not delete or modify it.

## Verification

The untouched base must continue to pass:

```sh
make test-ordered-d1-sde
```

Every multidimensional addition needs a scalar point-identity oracle and tests
across packet, block, and multi-block boundaries. Performance conclusions must
come from native AVX-512 timings; SDE is for correctness and instruction mix.

