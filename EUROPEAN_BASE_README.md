# European Option Engine

Private European option pricing engine built on the private Sobol-to-Gaussian
AVX-512 generator.

## Status

This project contains the exact buffer-reference pricing path, the private
generator sources, the fused Gaussian-exp kernel, and an experimental direct
payoff kernel that composes Gaussian, exp, and payoff into scheduled local
polynomials.

- `EUROPEAN_MODE_GAUSSIAN_EXP`: implemented. It keeps Gaussian values in zmm
  registers, computes `x = sigma*sqrt(T)*z`, approximates `exp(x)` with baked
  polynomial constants, applies call/put payoff, and reduces without Gaussian
  stores. Contract setup computes only scalar factors.
- `EUROPEAN_MODE_DIRECT_PAYOFF`: experimental. Contract setup derives local
  payoff polynomials from the deterministic Sobol slot, Gaussian affine
  coefficients, and option parameters. The hot path mantissa-injects Sobol,
  evaluates the local payoff polynomial, clamps, and reduces.
- `EUROPEAN_MODE_GAUSSIAN_EXP_REDUCED_FMA`: production-gated fast path for
  `sigma*sqrt(T) <= 0.20`. Contract setup composes the shared Gaussian affine
  map with the payoff line, while 14 deterministic hard-tail packets retain a
  compact cubic payoff schedule. Outside that envelope it falls back exactly
  to `EUROPEAN_MODE_GAUSSIAN_EXP`.
- `EUROPEAN_MODE_ORDERED_D1_MIN_FMA`: research-only consecutive-index D1
  layout. It accepts any positive multiple of 32 points, starts at canonical
  Sobol index 8192, advances two zmm states with derived +32 jump words, and
  uses two shared coefficient loads plus two FMAs per packet. A deterministic
  four-zmm correction pass repairs the 64 outer head/tail points in each 8192
  values without permuting the main states. The accurate envelope is
  `sigma*sqrt(T) <= 0.20`; larger ordered-D1 requests are rejected. Its
  contract-specific coefficient composition can be prepared once and reused
  through `european_price_prepared` or `european_price_prepared_points`.

For repeated valuation of an unchanged request, `european_prepare`,
`european_price_prepared`, and `european_prepared_destroy` move all coefficient
construction out of the pricing loop. A prepared object is immutable after
creation and may be shared by concurrent readers. Preparation accepts the
reduced-FMA and ordered-D1 modes; reduced-FMA fallback contracts are also
prepared once.

## Build

```sh
make
```

## Run

```sh
./bench_european 16 call
./bench_european 16 put
./bench_european 16 call gaussian-exp-reduced-fma
./bench_european 16 call gaussian-exp-reduced-fma-prepared 100
./bench_european_points 8192 call
```

## Test

```sh
make test
make test-reduced-fma-sde
make test-ordered-d1-sde
```
