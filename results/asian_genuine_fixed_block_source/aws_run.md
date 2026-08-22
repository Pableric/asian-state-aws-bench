# AWS execution

Build only the native benchmark and its compiled dependencies, then run the
bounded built-in preflight:

```sh
make -f tests/Makefile.asian_genuine_fixed_block_source \
  -j2 aws-benchmark-native
```

Run the native matrix with a new output path:

```sh
mkdir -p results/asian_genuine_fixed_block_source

MKL_THREADING_LAYER=SEQUENTIAL \
MKL_NUM_THREADS=1 \
MKL_DYNAMIC=FALSE \
./bench_asian_genuine_fixed_block_source \
  --json results/asian_genuine_fixed_block_source/aws.json
```

If that success path already exists, choose another explicit path. The
executable refuses to replace a successful artifact. It writes a sibling
temporary file and publishes with a no-replace atomic operation only after
success. Failures preserve any success and publish separate valid JSON.

The AWS dependency graph has no Python, NumPy, MPFR, Intel SDE, coefficient or
table generator, research audit, or network dependency. The generator,
independent MPFR verifier, and SDE structural audit are local qualification
tools only.

Never report the challenger as Sobol generation or inverse-CDF throughput.
Use `prepared fixed-block source consumption`; X3 is the general
generated-source baseline. TSC values are TSC units, never CPU cycles.
