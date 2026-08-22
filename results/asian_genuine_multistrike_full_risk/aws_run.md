# AWS execution

```sh
make -f tests/Makefile.asian_genuine_multistrike_full_risk \
  -j2 aws-benchmark-native

mkdir -p results/asian_genuine_multistrike_full_risk

MKL_THREADING_LAYER=SEQUENTIAL \
MKL_NUM_THREADS=1 \
MKL_DYNAMIC=FALSE \
./bench_asian_genuine_multistrike_full_risk \
  --json results/asian_genuine_multistrike_full_risk/aws.json
```

Choose a new explicit output path if `aws.json` already exists. The executable
will refuse to replace it.
