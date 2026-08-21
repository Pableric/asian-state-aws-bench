# Asian strip Delta qualification

Decision: **DELTA_QUALIFIED**

Package base: `1b199076ae9fa6db2258172587b59fef400e11ff`

## Pre-registered gates

| Gate | Result |
|---|---:|
| `frozen_source_hashes` | PASS |
| `price_qualification` | PASS |
| `complete_price_and_delta_decomposition` | PASS |
| `canonical_kink_path_471` | PASS |
| `analytic_geometric_price_delta` | PASS |
| `ordered_d1_generator` | PASS |
| `ordered_d1_layout` | PASS |
| `ordered_d1_kernel` | PASS |
| `sobol_exact_unique_expanding` | PASS |
| `price_and_tile_bits` | PASS |
| `same_state` | PASS |
| `smooth_residual` | PASS |
| `bump_agreement` | PASS |
| `parity` | PASS |
| `convergence` | PASS |

## Core evidence

- Same-Q/G maximum Delta error: `8.150527719e-08`.
- Signed mean same-Q/G error: `6.902766586e-11`.
- Maximum actual-leaf kink-adjusted smooth residual: `1.134527219e-07`.
- Signed mean actual-leaf smooth residual: `-1.036296456e-08`.
- Arithmetic/geometric ambiguous paths: `7` / `8`.
- Maximum inverse-normal CDF residual: `1.084202172e-19`.
- CRN bump expanded-CI coverage: `4880/4880` (`100.000000%`).
- Direct/parity expanded-CI coverage: `4880/4880` (`100.000000%`).

## Aggregate paired pathwise-minus-bump results

| N | Estimator | Side | Prefix | Bias | RMSE | Coverage |
|---:|---|---|---:|---:|---:|---:|
| 16 | arithmetic | call | 512 | 1.525581607e-05 | 4.586500641e-04 | 61/61 |
| 16 | arithmetic | call | 1024 | 2.777795934e-05 | 2.440866986e-04 | 61/61 |
| 16 | arithmetic | call | 2048 | 3.032311715e-05 | 1.150532676e-04 | 61/61 |
| 16 | arithmetic | call | 4096 | 7.685455049e-06 | 6.789302405e-05 | 61/61 |
| 16 | arithmetic | put | 512 | -1.531371126e-05 | 4.004396728e-04 | 61/61 |
| 16 | arithmetic | put | 1024 | -2.430742147e-05 | 2.154049590e-04 | 61/61 |
| 16 | arithmetic | put | 2048 | -2.782456439e-05 | 1.063405366e-04 | 61/61 |
| 16 | arithmetic | put | 4096 | -7.433431734e-06 | 6.141934769e-05 | 61/61 |
| 16 | geometric_cv | call | 512 | -2.053418696e-06 | 1.134628641e-04 | 61/61 |
| 16 | geometric_cv | call | 1024 | -3.348175188e-06 | 9.104790027e-05 | 61/61 |
| 16 | geometric_cv | call | 2048 | -4.708192158e-06 | 7.220654654e-05 | 61/61 |
| 16 | geometric_cv | call | 4096 | -3.131889099e-06 | 4.983223325e-05 | 61/61 |
| 16 | geometric_cv | put | 512 | -5.287132923e-06 | 1.137722562e-04 | 61/61 |
| 16 | geometric_cv | put | 1024 | -5.473373403e-06 | 9.091113550e-05 | 61/61 |
| 16 | geometric_cv | put | 2048 | -5.912780624e-06 | 7.220093034e-05 | 61/61 |
| 16 | geometric_cv | put | 4096 | -2.864995579e-06 | 4.987137122e-05 | 61/61 |
| 32 | arithmetic | call | 512 | 1.784908499e-05 | 4.511534715e-04 | 61/61 |
| 32 | arithmetic | call | 1024 | 2.506025846e-05 | 2.478851899e-04 | 61/61 |
| 32 | arithmetic | call | 2048 | 2.602812146e-05 | 1.216349615e-04 | 61/61 |
| 32 | arithmetic | call | 4096 | 1.499126755e-05 | 7.494446162e-05 | 61/61 |
| 32 | arithmetic | put | 512 | -1.004530051e-05 | 3.916933211e-04 | 61/61 |
| 32 | arithmetic | put | 1024 | -1.768391618e-05 | 2.189258303e-04 | 61/61 |
| 32 | arithmetic | put | 2048 | -1.913305354e-05 | 1.061268668e-04 | 61/61 |
| 32 | arithmetic | put | 4096 | -1.254860102e-05 | 6.600804019e-05 | 61/61 |
| 32 | geometric_cv | call | 512 | 3.890891998e-07 | 1.196293009e-04 | 61/61 |
| 32 | geometric_cv | call | 1024 | 1.690938536e-06 | 8.278882462e-05 | 61/61 |
| 32 | geometric_cv | call | 2048 | 4.514031825e-07 | 6.389281156e-05 | 61/61 |
| 32 | geometric_cv | call | 4096 | -5.116821383e-07 | 4.838423994e-05 | 61/61 |
| 32 | geometric_cv | put | 512 | -4.616885872e-06 | 1.198069065e-04 | 61/61 |
| 32 | geometric_cv | put | 1024 | -2.929095112e-07 | 8.395657142e-05 | 61/61 |
| 32 | geometric_cv | put | 2048 | 5.097439808e-07 | 6.372912142e-05 | 61/61 |
| 32 | geometric_cv | put | 4096 | -6.221431265e-07 | 4.824204145e-05 | 61/61 |
| 64 | arithmetic | call | 512 | 1.522880056e-05 | 4.334289040e-04 | 61/61 |
| 64 | arithmetic | call | 1024 | 3.564958522e-05 | 2.694075414e-04 | 61/61 |
| 64 | arithmetic | call | 2048 | 2.002165275e-05 | 1.283775424e-04 | 61/61 |
| 64 | arithmetic | call | 4096 | 1.334940380e-05 | 7.338576394e-05 | 61/61 |
| 64 | arithmetic | put | 512 | -1.776362606e-05 | 3.775331822e-04 | 61/61 |
| 64 | arithmetic | put | 1024 | -3.040234902e-05 | 2.362852166e-04 | 61/61 |
| 64 | arithmetic | put | 2048 | -1.652718690e-05 | 1.116110949e-04 | 61/61 |
| 64 | arithmetic | put | 4096 | -1.005568833e-05 | 6.370540740e-05 | 61/61 |
| 64 | geometric_cv | call | 512 | -9.614135615e-06 | 1.342419809e-04 | 61/61 |
| 64 | geometric_cv | call | 1024 | -6.915080577e-06 | 9.103868817e-05 | 61/61 |
| 64 | geometric_cv | call | 2048 | -5.794397156e-07 | 6.171703887e-05 | 61/61 |
| 64 | geometric_cv | call | 4096 | -8.779772859e-07 | 4.517594798e-05 | 61/61 |
| 64 | geometric_cv | put | 512 | -7.056618754e-06 | 1.339475563e-04 | 61/61 |
| 64 | geometric_cv | put | 1024 | -3.284227929e-06 | 9.005809971e-05 | 61/61 |
| 64 | geometric_cv | put | 2048 | -2.955540729e-06 | 6.093518715e-05 | 61/61 |
| 64 | geometric_cv | put | 4096 | -6.844836998e-07 | 4.475247026e-05 | 61/61 |
| 128 | arithmetic | call | 512 | -2.595991072e-05 | 4.850880207e-04 | 61/61 |
| 128 | arithmetic | call | 1024 | 6.762212961e-06 | 2.857801081e-04 | 61/61 |
| 128 | arithmetic | call | 2048 | 1.083493045e-05 | 1.321338393e-04 | 61/61 |
| 128 | arithmetic | call | 4096 | 9.812079239e-06 | 7.002428160e-05 | 61/61 |
| 128 | arithmetic | put | 512 | 2.660782985e-05 | 4.168351186e-04 | 61/61 |
| 128 | arithmetic | put | 1024 | 3.245916378e-07 | 2.506448869e-04 | 61/61 |
| 128 | arithmetic | put | 2048 | -4.446149875e-06 | 1.155893767e-04 | 61/61 |
| 128 | arithmetic | put | 4096 | -4.788548041e-06 | 6.354433348e-05 | 61/61 |
| 128 | geometric_cv | call | 512 | -2.456961025e-06 | 1.541816987e-04 | 61/61 |
| 128 | geometric_cv | call | 1024 | 5.835551302e-07 | 1.019851211e-04 | 61/61 |
| 128 | geometric_cv | call | 2048 | 2.881833646e-06 | 7.069685109e-05 | 61/61 |
| 128 | geometric_cv | call | 4096 | 2.365207659e-06 | 4.658498107e-05 | 61/61 |
| 128 | geometric_cv | put | 512 | 9.400788510e-06 | 1.507076112e-04 | 61/61 |
| 128 | geometric_cv | put | 1024 | 8.340974373e-06 | 1.007048521e-04 | 61/61 |
| 128 | geometric_cv | put | 2048 | 1.189920839e-06 | 7.007809132e-05 | 61/61 |
| 128 | geometric_cv | put | 4096 | 1.363876290e-06 | 4.624558333e-05 | 61/61 |
| 256 | arithmetic | call | 512 | -2.189973627e-05 | 5.075605745e-04 | 61/61 |
| 256 | arithmetic | call | 1024 | 1.419601320e-05 | 2.703285991e-04 | 61/61 |
| 256 | arithmetic | call | 2048 | 1.729830379e-06 | 1.279338636e-04 | 61/61 |
| 256 | arithmetic | call | 4096 | 4.118781564e-06 | 7.223967830e-05 | 61/61 |
| 256 | arithmetic | put | 512 | 1.104034696e-05 | 4.450358262e-04 | 61/61 |
| 256 | arithmetic | put | 1024 | -1.322559650e-05 | 2.361352029e-04 | 61/61 |
| 256 | arithmetic | put | 2048 | 1.440583017e-06 | 1.132122341e-04 | 61/61 |
| 256 | arithmetic | put | 4096 | -1.073516862e-06 | 6.689245946e-05 | 61/61 |
| 256 | geometric_cv | call | 512 | -1.144295998e-05 | 1.577542199e-04 | 61/61 |
| 256 | geometric_cv | call | 1024 | -1.341569805e-06 | 8.806230730e-05 | 61/61 |
| 256 | geometric_cv | call | 2048 | 5.655416729e-06 | 6.846643785e-05 | 61/61 |
| 256 | geometric_cv | call | 4096 | 1.001587749e-06 | 4.965662549e-05 | 61/61 |
| 256 | geometric_cv | put | 512 | 4.826171581e-07 | 1.509252149e-04 | 61/61 |
| 256 | geometric_cv | put | 1024 | -2.323553829e-06 | 8.678971361e-05 | 61/61 |
| 256 | geometric_cv | put | 2048 | 3.491257307e-06 | 6.688441507e-05 | 61/61 |
| 256 | geometric_cv | put | 4096 | 1.845565357e-06 | 4.949231003e-05 | 61/61 |

## Convergence

| Prefix | Pooled bias | Pooled RMSE | Median estimator SE |
|---:|---:|---:|---:|
| 512 | -1.862801218e-06 | 3.246073502e-04 | 6.964347061e-04 |
| 1024 | 5.893957721e-07 | 1.870535287e-04 | 4.827221402e-04 |
| 2048 | 1.123545148e-06 | 9.610109580e-05 | 3.426349470e-04 |
| 4096 | 5.970133668e-07 | 5.888136566e-05 | 2.481721559e-04 |

## Worst cases and kink decomposition

| Kind | N | Count | Strike | Estimator | Side | Prefix | Rep | Unadjusted | Flip | Residual |
|---|---:|---:|---:|---|---|---:|---:|---:|---:|---:|
| same_state | 32 | 4 | 100.5 | arithmetic | call | 4096 | 28 | -8.150527719e-08 | 0.000000000e+00 | -8.150527719e-08 |
| smooth_residual | 32 | 4 | 100.5 | arithmetic | call | 4096 | 28 | -1.134527219e-07 | 0.000000000e+00 | -1.134527219e-07 |
| unadjusted_kink | 64 | 4 | 120 | geometric_cv | put | 4096 | 29 | -2.843224498e-04 | -2.843101781e-04 | -1.227175016e-08 |

Canonical unshifted N=128/K=98 remains explicit: path 471, unadjusted `2.321680278e-04`, flip `2.321866257e-04`, and residual `-1.859787887e-08`.

## Reproduction on native AWS

```sh
make -f tests/Makefile.asian_genuine_delta_qualification -j2 qualification-native
python3 tests/audit_asian_genuine_delta_qualification.py
```

The runner invokes no Intel SDE and performs no performance tuning.
