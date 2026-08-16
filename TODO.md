# TODO

- Freeze and retain the corrected ordered-D1 European result as the initial
  regression baseline.

- Add a preparation-time mapping for caller-selected D2/other dimensions while
  retaining the current 32-path packet layout and exact Sobol point identity.

- Integrate and natively compare the parameterized D1 reconstruction fragments:
  one-line `vpermd`, two-line `vpermi2d`, and direct in-register generation.
  Assign physical registers only after checking live ranges in the current hot
  loop. Do not add an intermediate output buffer by assumption.

- Build a scalar multidimensional reference from
  `direction_numbers/openevolve.json` and test every produced raw word and lane
  mapping across multiple 8,192-point blocks.

- Once dimension construction is correct and measured, replace the European
  terminal payoff with a 32-fixing Asian `(S,Q)` state transition. Use the
  preserved August prototype only as reference; reassess Brownian bridge versus
  forward construction for the new architecture.

- Extend correctness tests before Gaussian, exponential, or tail optimization.

