# Seasoned native-overhead confirmation

Decision: `SEASONED_STRIP_QUALIFIED`.

This analysis compares only matched-f unseasoned (A) and seasoned (B) tile-8 price-plus-Delta executions. Every observation is an ABBA/BAAB quartet and uses `(B1+B2)/(A1+A2)`.

## Global pre-registered gates

| Clock | Geometric mean | Bootstrap 95% upper | Limits |
|---|---:|---:|---:|
| TSC | 1.000702219 | 1.001324303 | 1.005 / 1.01 |
| Wall | 1.000685093 | 1.001313254 | 1.005 / 1.01 |

Maximum individual cell medians are 1.006433653 TSC and 1.006411198 wall, against the 1.02 hard guard.

## Byte-identical negative controls

| Clock | Geometric mean | Two-sided bootstrap 95% interval |
|---|---:|---:|
| TSC | 1.000984636 | [0.999549434, 1.002529536] |
| Wall | 1.000932779 | [0.999533233, 1.002516433] |

The controls are methodologically valid: both intervals must contain 1.0.

## Cell results

Individual confidence intervals are diagnostic only.

| M | c | f | Estimator | Cache mode | TSC median [95%] | Wall median [95%] |
|---:|---:|---:|---|---|---:|---:|
| 16 | 0 | 16 | arithmetic | candidate_specific_warm | 1.000548379 [0.999161710, 1.001656356] | 1.000429185 [0.999164917, 1.001578645] |
| 16 | 0 | 16 | arithmetic | historical_32KiB_rmw | 1.000386182 [0.999470429, 1.000979628] | 1.000144913 [0.999327476, 1.001034467] |
| 16 | 0 | 16 | geometric_cv | candidate_specific_warm | 0.999591869 [0.998869324, 1.000195383] | 0.999662566 [0.998938789, 1.000237791] |
| 16 | 0 | 16 | geometric_cv | historical_32KiB_rmw | 1.000151192 [0.999544503, 1.000577852] | 1.000049343 [0.999572284, 1.000707761] |
| 128 | 32 | 96 | arithmetic | candidate_specific_warm | 0.999702161 [0.995050140, 1.003682425] | 0.999714624 [0.995044335, 1.003716220] |
| 128 | 32 | 96 | arithmetic | historical_32KiB_rmw | 1.000008522 [0.998933035, 1.000746022] | 0.999928069 [0.998938677, 1.000712758] |
| 128 | 32 | 96 | geometric_cv | candidate_specific_warm | 0.999277234 [0.995634340, 1.003793355] | 0.999348395 [0.995638427, 1.003764395] |
| 128 | 32 | 96 | geometric_cv | historical_32KiB_rmw | 0.999992874 [0.995988971, 1.003713243] | 1.000000000 [0.996018897, 1.003771288] |
| 256 | 0 | 256 | arithmetic | candidate_specific_warm | 1.001281071 [0.993992680, 1.007483170] | 1.001226180 [0.993992991, 1.007739378] |
| 256 | 0 | 256 | arithmetic | historical_32KiB_rmw | 0.999993172 [0.998616764, 1.001325297] | 1.000008665 [0.998700798, 1.001427618] |
| 256 | 0 | 256 | geometric_cv | candidate_specific_warm | 1.005449610 [0.999832834, 1.012235272] | 1.005498163 [0.999883013, 1.012277904] |
| 256 | 0 | 256 | geometric_cv | historical_32KiB_rmw | 1.000487787 [0.996058410, 1.006144543] | 1.000455816 [0.996160281, 1.006109438] |
| 256 | 1 | 255 | arithmetic | candidate_specific_warm | 1.005548478 [0.999470389, 1.009182177] | 1.005523193 [0.999478859, 1.009347910] |
| 256 | 1 | 255 | arithmetic | historical_32KiB_rmw | 1.000419661 [0.999592104, 1.001299182] | 1.000406915 [0.999590240, 1.001227042] |
| 256 | 1 | 255 | geometric_cv | candidate_specific_warm | 1.000189064 [0.993078592, 1.008028800] | 1.000186219 [0.993034302, 1.008126582] |
| 256 | 1 | 255 | geometric_cv | historical_32KiB_rmw | 1.000209326 [0.994244135, 1.006117446] | 1.000222663 [0.994222551, 1.006083682] |
| 256 | 64 | 192 | arithmetic | candidate_specific_warm | 1.001200049 [0.994168461, 1.006215992] | 1.001175437 [0.994155201, 1.006225320] |
| 256 | 64 | 192 | arithmetic | historical_32KiB_rmw | 0.999823868 [0.994586042, 1.000565558] | 0.999827906 [0.994578960, 1.000544581] |
| 256 | 64 | 192 | geometric_cv | candidate_specific_warm | 1.006433653 [1.000384276, 1.012801980] | 1.006411198 [1.000364498, 1.012793786] |
| 256 | 64 | 192 | geometric_cv | historical_32KiB_rmw | 0.999805569 [0.993874194, 1.006620056] | 0.999782106 [0.993792788, 1.006650807] |
| 256 | 128 | 128 | arithmetic | candidate_specific_warm | 1.000133786 [0.994113813, 1.006057486] | 1.000160303 [0.994062988, 1.006016327] |
| 256 | 128 | 128 | arithmetic | historical_32KiB_rmw | 1.000922805 [1.000033240, 1.005864866] | 1.000963898 [1.000022536, 1.005810205] |
| 256 | 128 | 128 | geometric_cv | candidate_specific_warm | 0.999476711 [0.994911104, 1.004293901] | 0.999463463 [0.994921287, 1.004312819] |
| 256 | 128 | 128 | geometric_cv | historical_32KiB_rmw | 0.999912697 [0.995465928, 1.004792034] | 0.999965166 [0.995446323, 1.004644746] |
| 256 | 255 | 1 | arithmetic | candidate_specific_warm | 0.999504499 [0.998957709, 1.000547845] | 0.999604165 [0.998963509, 1.000653249] |
| 256 | 255 | 1 | arithmetic | historical_32KiB_rmw | 0.999237960 [0.998117384, 1.000559611] | 0.999149376 [0.998058429, 1.000666586] |
| 256 | 255 | 1 | geometric_cv | candidate_specific_warm | 0.999907966 [0.999642753, 1.000174143] | 0.999844615 [0.999559083, 1.000156166] |
| 256 | 255 | 1 | geometric_cv | historical_32KiB_rmw | 1.000113278 [0.999822090, 1.000291786] | 1.000077426 [0.999786805, 1.000348682] |

## Provenance and protocol correction

Raw quartets: `results/asian_genuine_seasoned_overhead_confirmation/raw_aws.json` (SHA-256 `ee188cb05be6ea180d2fcdbdd84b49af8cdf04bedfb2b55bccc4c40c5069b887`).

Benchmark binary SHA-256: `7e269a4dab8bc88c91315c4be84f42a4e09e1618fb615df9491ac1b7c5fabef9`.

Preserved original failed AWS JSON: [`results/asian_genuine_seasoned_price_delta_strip/aws.json`](../asian_genuine_seasoned_price_delta_strip/aws.json) (SHA-256 `c9be6b39fcf39674487b5f9c8626c6f48be0022b7653baeef6c39c537b149857`).

The original 400/400 rule treated hundreds of correlated case/candidate comparisons as independent mandatory hypothesis tests. Requiring every noisy individual interval to pass makes the family-wise rejection probability grow with the matrix and turns ordinary timer noise into near-certain failure. This confirmation instead pre-registers one stratified global statistic, keeps a bounded per-cell effect guard, and uses the byte-identical c=0 cells to detect protocol bias.

Bootstrap: 10000 deterministic stratified resamples, seed `0x534541534f564552`. Tile 8 was frozen before observing these data.

Qualification statuses:

- `SEASONED_NATIVE_OVERHEAD_CONFIRMED`
- `SEASONED_NATIVE_PERFORMANCE_QUALIFIED`
- `SEASONED_STRIP_QUALIFIED`
