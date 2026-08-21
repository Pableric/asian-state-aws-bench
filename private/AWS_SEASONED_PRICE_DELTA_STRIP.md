# Seasoned price/Delta strip AWS package

The AWS workflow is performance-only.  Its prerequisites are the committed
local qualification report and structural audit; AWS does not regenerate or
reinterpret either one.

Build and execute only the benchmark's native preflight:

```sh
make -f tests/Makefile.asian_genuine_seasoned_strip -j2 aws-benchmark-native
```

Then collect the 16-warmup, 51-sample native run on the first permitted
physical CPU:

```sh
mkdir -p results/asian_genuine_seasoned_price_delta_strip
MKL_THREADING_LAYER=SEQUENTIAL MKL_NUM_THREADS=1 MKL_DYNAMIC=FALSE \
  ./bench_asian_genuine_seasoned_price_delta_strip \
  --json results/asian_genuine_seasoned_price_delta_strip/aws.json
```

The target does not invoke Python, NumPy, MPFR, Intel SDE, coefficient
generation, replication qualification, or report generation.  It builds the
single benchmark executable and runs only its built-in native correctness
preflight.  The benchmark covers every principal `(M,c)` pair, both cache
modes, both estimators, price and price-plus-Delta, tile 4 and tile 8, and
matched-f unseasoned, seasoned genuine, and seasoned oneMKL providers.

Each seasoned/unseasoned 51-sample pairing is accepted only when the median
ratio is at most `1.01` and the 33rd ordered ratio (the pre-registered exact
nonparametric median 95% upper endpoint) is at most `1.02`, independently for
TSC and monotonic wall time.  Until those native gates pass, the committed
status remains `SEASONED_CORRECTNESS_QUALIFIED_AWS_PERFORMANCE_PENDING`.
