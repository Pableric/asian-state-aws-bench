# Seasoned Asian Delta qualification

Decision: `SEASONED_CORRECTNESS_QUALIFIED_AWS_PERFORMANCE_PENDING`.

| Gate | Result | Evidence |
|---|---:|---:|
| Sobol exact/unique/expanding | PASS | mismatches=0, duplicates=0, expansion=0 |
| Same-state Delta | PASS | max 8.15052772e-08, signed mean -3.34569959e-11 |
| Smooth Delta | PASS | max 1.61329431e-07, signed mean -6.31006325e-09 |
| CRN bump coverage | PASS | 12800/12800, worst ratio 0.871536 |
| Convergence | PASS | RMSE 0.000243719 -> 4.22076e-05; median SE 0.000508283 -> 0.000180998 |

## Aggregate CRN table

| Estimator | Side | Prefix | Bias | RMSE | Coverage |
|---|---|---:|---:|---:|---:|
| arithmetic | call | 512 | 1.64581e-06 | 2.37209e-05 | 1 |
| arithmetic | call | 1024 | 1.41433e-05 | 2.64025e-05 | 1 |
| arithmetic | call | 2048 | 1.19556e-05 | 2.2442e-05 | 1 |
| arithmetic | call | 4096 | 5.73147e-06 | 1.09409e-05 | 1 |
| arithmetic | put | 512 | -2.67489e-06 | 2.37379e-05 | 1 |
| arithmetic | put | 1024 | -1.34922e-05 | 2.61426e-05 | 1 |
| arithmetic | put | 2048 | -1.14358e-05 | 2.21207e-05 | 1 |
| arithmetic | put | 4096 | -5.31648e-06 | 1.06038e-05 | 1 |
| geometric CV | call | 512 | -2.01799e-07 | 1.84739e-05 | 1 |
| geometric CV | call | 1024 | 2.14603e-06 | 1.30168e-05 | 1 |
| geometric CV | call | 2048 | 2.80208e-06 | 1.00211e-05 | 1 |
| geometric CV | call | 4096 | 7.90838e-07 | 6.19685e-06 | 1 |
| geometric CV | put | 512 | -5.28011e-07 | 1.83069e-05 | 1 |
| geometric CV | put | 1024 | -2.28649e-06 | 1.29853e-05 | 1 |
| geometric CV | put | 2048 | -2.72445e-06 | 9.94779e-06 | 1 |
| geometric CV | put | 4096 | -7.82124e-07 | 6.11507e-06 | 1 |

All 26 arithmetic and 37 geometric ambiguous observations are retained in `kinks.jsonl`.
