#include "tests/autocall_two_asset_three_date_mvn_program_cases.h"
#include "tests/autocall_two_asset_three_date_worstof_cases.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

namespace {

struct ExtraSpec {
    const char *name;
    double t[3];
    double spot_a;
    double spot_b;
    double sigma_a;
    double sigma_b;
    double q_a;
    double q_b;
    double rate;
    double rho;
    double protection;
    double call3_a;
    double call3_b;
    unsigned style;
};

/* Explicit selection values. No candidate-derived value may enter this table. */
static constexpr ExtraSpec SELECTION_EXTRA[16] = {
    {"MVN_NONUNIFORM", {0x1.5555555555555p-3, 0x1.199999999999ap-1, 0x1p+0}, 0x1.9p+6, 0x1.9p+6, 0x1.999999999999ap-3, 0x1.3333333333333p-2, 0x1.47ae147ae147bp-7, 0x1.eb851eb851eb8p-7, 0x1.eb851eb851eb8p-6, 0x1p-1, 0x1.4cccccccccccdp-1, 0x1p+0, 0x1p+0, 0},
    {"MVN_CLOSE_DATES", {0x1.f5c28f5c28f5cp-2, 0x1p-1, 0x1p+0}, 0x1.9p+6, 0x1.9p+6, 0x1.999999999999ap-3, 0x1.999999999999ap-3, 0x1.47ae147ae147bp-7, 0x1.47ae147ae147bp-7, 0x1.eb851eb851eb8p-6, 0x1.ccccccccccccdp-1, 0x1.4cccccccccccdp-1, 0x1p+0, 0x1p+0, 1},
    {"MVN_RHO_POS_ONE", {0x1.5555555555555p-2, 0x1.5555555555555p-1, 0x1p+0}, 0x1.8p+6, 0x1.ap+6, 0x1.999999999999ap-3, 0x1.3333333333333p-2, 0x1.47ae147ae147bp-7, 0x1.eb851eb851eb8p-7, 0x1.eb851eb851eb8p-6, 0x1p+0, 0x1.4cccccccccccdp-1, 0x1p+0, 0x1p+0, 2},
    {"MVN_RHO_NEG_ONE", {0x1.5555555555555p-2, 0x1.5555555555555p-1, 0x1p+0}, 0x1.8p+6, 0x1.ap+6, 0x1.999999999999ap-3, 0x1.3333333333333p-2, 0x1.47ae147ae147bp-7, 0x1.eb851eb851eb8p-7, 0x1.eb851eb851eb8p-6, -0x1p+0, 0x1.4cccccccccccdp-1, 0x1p+0, 0x1p+0, 3},
    {"MVN_RHO_NEAR_POS", {0x1.5555555555555p-2, 0x1.5555555555555p-1, 0x1p+0}, 0x1.9p+6, 0x1.9p+6, 0x1.999999999999ap-3, 0x1.999999999999ap-3, 0x1.47ae147ae147bp-7, 0x1.47ae147ae147bp-7, 0x1.eb851eb851eb8p-6, 0x1.ffffffffffffp-1, 0x1.4cccccccccccdp-1, 0x1p+0, 0x1p+0, 4},
    {"MVN_RHO_NEAR_NEG", {0x1.5555555555555p-2, 0x1.5555555555555p-1, 0x1p+0}, 0x1.9p+6, 0x1.9p+6, 0x1.999999999999ap-3, 0x1.999999999999ap-3, 0x1.47ae147ae147bp-7, 0x1.47ae147ae147bp-7, 0x1.eb851eb851eb8p-6, -0x1.ffffffffffffp-1, 0x1.4cccccccccccdp-1, 0x1p+0, 0x1p+0, 5},
    {"MVN_UNEQUAL_VOL", {0x1.3333333333333p-2, 0x1.199999999999ap-1, 0x1.199999999999ap+0}, 0x1.6p+6, 0x1.cp+6, 0x1.0a3d70a3d70a4p-2, 0x1.199999999999ap-1, -0x1.47ae147ae147bp-7, 0x1.47ae147ae147bp-6, 0x1.47ae147ae147bp-5, 0x1.3333333333333p-2, 0x1.999999999999ap-1, 0x1.0cccccccccccdp+0, 0x1p+0, 6},
    {"MVN_ASYMMETRIC_BARRIERS", {0x1p-2, 0x1.4cccccccccccdp-1, 0x1.0cccccccccccdp+0}, 0x1.7p+6, 0x1.bp+6, 0x1.999999999999ap-3, 0x1.3333333333333p-2, 0x1.47ae147ae147bp-7, 0x1.eb851eb851eb8p-7, 0x1.eb851eb851eb8p-6, 0x1.3333333333333p-1, 0x1.599999999999ap-1, 0x1.0cccccccccccdp+0, 0x1.199999999999ap+0, 7},
    {"MVN_DEEP_LOWER", {0x1.5555555555555p-2, 0x1.5555555555555p-1, 0x1p+0}, 0x1.9p+6, 0x1.9p+6, 0x1.999999999999ap-3, 0x1.999999999999ap-3, 0x1.47ae147ae147bp-7, 0x1.47ae147ae147bp-7, 0x1.eb851eb851eb8p-6, 0x1p-1, 0x1.0cccccccccccdp-1, 0x1p+0, 0x1p+0, 8},
    {"MVN_DEEP_UPPER", {0x1.5555555555555p-2, 0x1.5555555555555p-1, 0x1p+0}, 0x1.9p+6, 0x1.9p+6, 0x1.999999999999ap-3, 0x1.999999999999ap-3, 0x1.47ae147ae147bp-7, 0x1.47ae147ae147bp-7, 0x1.eb851eb851eb8p-6, 0x1p-1, 0x1.8p-1, 0x1.8p+0, 0x1.8p+0, 9},
    {"MVN_NARROW_RECT", {0x1.4p-2, 0x1.4p-1, 0x1.ep-1}, 0x1.9p+6, 0x1.9p+6, 0x1.0a3d70a3d70a4p-2, 0x1.0a3d70a3d70a4p-2, 0x1.47ae147ae147bp-7, 0x1.47ae147ae147bp-7, 0x1.47ae147ae147bp-5, 0x1.ccccccccccccdp-1, 0x1.999999999999ap-1, 0x1p+0, 0x1p+0, 10},
    {"MVN_HIGH_CANCELLATION", {0x1.5555555555555p-2, 0x1.5555555555555p-1, 0x1p+0}, 0x1.9p+6, 0x1.9p+6, 0x1.999999999999ap-3, 0x1.999999999999ap-3, 0x1.47ae147ae147bp-7, 0x1.47ae147ae147bp-7, 0x1.eb851eb851eb8p-6, 0x1.999999999999ap-1, 0x1.4cccccccccccdp-1, 0x1.0000000000001p+0, 0x1.0000000000001p+0, 11},
    {"MVN_WEAKEST_SWITCH_A", {0x1.5555555555555p-2, 0x1.5555555555555p-1, 0x1p+0}, 0x1.8p+6, 0x1.ap+6, 0x1.47ae147ae147bp-2, 0x1.3333333333333p-2, 0x1.47ae147ae147bp-7, 0x1.47ae147ae147bp-7, 0x1.eb851eb851eb8p-6, 0x1p-2, 0x1.4cccccccccccdp-1, 0x1p+0, 0x1p+0, 12},
    {"MVN_WEAKEST_SWITCH_B", {0x1.5555555555555p-2, 0x1.5555555555555p-1, 0x1p+0}, 0x1.ap+6, 0x1.8p+6, 0x1.3333333333333p-2, 0x1.47ae147ae147bp-2, 0x1.47ae147ae147bp-7, 0x1.47ae147ae147bp-7, 0x1.eb851eb851eb8p-6, 0x1p-2, 0x1.4cccccccccccdp-1, 0x1p+0, 0x1p+0, 13},
    {"MVN_LAGGED_NONUNIFORM", {0x1.999999999999ap-3, 0x1.0cccccccccccdp-1, 0x1.199999999999ap+0}, 0x1.9p+6, 0x1.9p+6, 0x1.999999999999ap-3, 0x1.3333333333333p-2, 0x1.47ae147ae147bp-7, 0x1.eb851eb851eb8p-7, 0x1.eb851eb851eb8p-6, 0x1p-1, 0x1.4cccccccccccdp-1, 0x1p+0, 0x1p+0, 14},
    {"MVN_NEGATIVE_RATE", {0x1.5555555555555p-2, 0x1.5555555555555p-1, 0x1p+0}, 0x1.9p+6, 0x1.9p+6, 0x1.999999999999ap-3, 0x1.3333333333333p-2, 0x1.47ae147ae147bp-7, -0x1.47ae147ae147bp-6, -0x1.47ae147ae147bp-5, -0x1p-1, 0x1.4cccccccccccdp-1, 0x1p+0, 0x1p+0, 15},
};

/* Explicit unseen values. These bytes are hashed but never evaluated locally. */
static constexpr ExtraSpec HOLDOUT_EXTRA[16] = {
    {"HOLDOUT_NONUNIFORM", {0x1.5c28f5c28f5c3p-3, 0x1.3851eb851eb85p-1, 0x1.2147ae147ae14p+0}, 0x1.88p+6, 0x1.a4p+6, 0x1.b851eb851eb85p-3, 0x1.570a3d70a3d71p-2, 0x1.999999999999ap-7, 0x1.0a3d70a3d70a4p-6, 0x1.47ae147ae147bp-6, 0x1.0cccccccccccdp-1, 0x1.51eb851eb851fp-1, 0x1.0147ae147ae14p+0, 0x1.028f5c28f5c29p+0, 0},
    {"HOLDOUT_CLOSE_DATES", {0x1.9p-2, 0x1.904p-2, 0x1.199999999999ap+0}, 0x1.98p+6, 0x1.84p+6, 0x1.c28f5c28f5c29p-3, 0x1.1eb851eb851ecp-2, -0x1.47ae147ae147bp-7, 0x1.47ae147ae147bp-6, 0x1.999999999999ap-6, 0x1.d70a3d70a3d71p-1, 0x1.3d70a3d70a3d7p-1, 0x1.028f5c28f5c29p+0, 0x1.028f5c28f5c29p+0, 1},
    {"HOLDOUT_RHO_POS_ONE", {0x1.47ae147ae147bp-2, 0x1.47ae147ae147bp-1, 0x1.0cccccccccccdp+0}, 0x1.78p+6, 0x1.b8p+6, 0x1.a3d70a3d70a3dp-3, 0x1.5c28f5c28f5c3p-2, 0x1.999999999999ap-7, 0x1.0a3d70a3d70a4p-6, 0x1.47ae147ae147bp-6, 0x1p+0, 0x1.51eb851eb851fp-1, 0x1.0147ae147ae14p+0, 0x1.028f5c28f5c29p+0, 2},
    {"HOLDOUT_RHO_NEG_ONE", {0x1.47ae147ae147bp-2, 0x1.47ae147ae147bp-1, 0x1.0cccccccccccdp+0}, 0x1.78p+6, 0x1.b8p+6, 0x1.a3d70a3d70a3dp-3, 0x1.5c28f5c28f5c3p-2, 0x1.999999999999ap-7, 0x1.0a3d70a3d70a4p-6, 0x1.47ae147ae147bp-6, -0x1p+0, 0x1.51eb851eb851fp-1, 0x1.0147ae147ae14p+0, 0x1.028f5c28f5c29p+0, 3},
    {"HOLDOUT_RHO_NEAR_POS", {0x1.47ae147ae147bp-2, 0x1.47ae147ae147bp-1, 0x1.0cccccccccccdp+0}, 0x1.98p+6, 0x1.88p+6, 0x1.c28f5c28f5c29p-3, 0x1.147ae147ae148p-2, 0x1.999999999999ap-7, 0x1.0a3d70a3d70a4p-6, 0x1.47ae147ae147bp-6, 0x1.fffffffffffep-1, 0x1.51eb851eb851fp-1, 0x1.0147ae147ae14p+0, 0x1.028f5c28f5c29p+0, 4},
    {"HOLDOUT_RHO_NEAR_NEG", {0x1.47ae147ae147bp-2, 0x1.47ae147ae147bp-1, 0x1.0cccccccccccdp+0}, 0x1.98p+6, 0x1.88p+6, 0x1.c28f5c28f5c29p-3, 0x1.147ae147ae148p-2, 0x1.999999999999ap-7, 0x1.0a3d70a3d70a4p-6, 0x1.47ae147ae147bp-6, -0x1.fffffffffffep-1, 0x1.51eb851eb851fp-1, 0x1.0147ae147ae14p+0, 0x1.028f5c28f5c29p+0, 5},
    {"HOLDOUT_UNEQUAL_VOL", {0x1.199999999999ap-2, 0x1.0cccccccccccdp-1, 0x1.2p+0}, 0x1.68p+6, 0x1.d8p+6, 0x1.0a3d70a3d70a4p-2, 0x1.1eb851eb851ecp-1, -0x1.999999999999ap-7, 0x1.47ae147ae147bp-6, 0x1.999999999999ap-5, 0x1.5c28f5c28f5c3p-2, 0x1.999999999999ap-1, 0x1.0a3d70a3d70a4p+0, 0x1.051eb851eb852p+0, 6},
    {"HOLDOUT_ASYMMETRIC", {0x1.199999999999ap-2, 0x1.3333333333333p-1, 0x1.2666666666666p+0}, 0x1.74p+6, 0x1.c4p+6, 0x1.a3d70a3d70a3dp-3, 0x1.47ae147ae147bp-2, 0x1.999999999999ap-7, 0x1.0a3d70a3d70a4p-6, 0x1.47ae147ae147bp-6, 0x1.47ae147ae147bp-1, 0x1.5c28f5c28f5c3p-1, 0x1.0f5c28f5c28f6p+0, 0x1.1c28f5c28f5c3p+0, 7},
    {"HOLDOUT_DEEP_LOWER", {0x1.47ae147ae147bp-2, 0x1.47ae147ae147bp-1, 0x1.0cccccccccccdp+0}, 0x1.98p+6, 0x1.88p+6, 0x1.c28f5c28f5c29p-3, 0x1.147ae147ae148p-2, 0x1.999999999999ap-7, 0x1.0a3d70a3d70a4p-6, 0x1.47ae147ae147bp-6, 0x1.0cccccccccccdp-1, 0x1.0f5c28f5c28f6p-1, 0x1.0147ae147ae14p+0, 0x1.028f5c28f5c29p+0, 8},
    {"HOLDOUT_DEEP_UPPER", {0x1.47ae147ae147bp-2, 0x1.47ae147ae147bp-1, 0x1.0cccccccccccdp+0}, 0x1.98p+6, 0x1.88p+6, 0x1.c28f5c28f5c29p-3, 0x1.147ae147ae148p-2, 0x1.999999999999ap-7, 0x1.0a3d70a3d70a4p-6, 0x1.47ae147ae147bp-6, 0x1.0cccccccccccdp-1, 0x1.7ae147ae147aep-1, 0x1.7ae147ae147aep+0, 0x1.7ae147ae147aep+0, 9},
    {"HOLDOUT_NARROW", {0x1.5c28f5c28f5c3p-2, 0x1.5c28f5c28f5c3p-1, 0x1.051eb851eb852p+0}, 0x1.98p+6, 0x1.88p+6, 0x1.147ae147ae148p-2, 0x1.147ae147ae148p-2, 0x1.999999999999ap-7, 0x1.0a3d70a3d70a4p-6, 0x1.999999999999ap-5, 0x1.d1eb851eb851fp-1, 0x1.9eb851eb851ecp-1, 0x1.0147ae147ae14p+0, 0x1.028f5c28f5c29p+0, 10},
    {"HOLDOUT_CANCELLATION", {0x1.47ae147ae147bp-2, 0x1.47ae147ae147bp-1, 0x1.0cccccccccccdp+0}, 0x1.98p+6, 0x1.88p+6, 0x1.c28f5c28f5c29p-3, 0x1.147ae147ae148p-2, 0x1.999999999999ap-7, 0x1.0a3d70a3d70a4p-6, 0x1.47ae147ae147bp-6, 0x1.9eb851eb851ecp-1, 0x1.51eb851eb851fp-1, 0x1.0000000000002p+0, 0x1.0000000000002p+0, 11},
    {"HOLDOUT_SWITCH_A", {0x1.47ae147ae147bp-2, 0x1.47ae147ae147bp-1, 0x1.0cccccccccccdp+0}, 0x1.7cp+6, 0x1.b4p+6, 0x1.5c28f5c28f5c3p-2, 0x1.28f5c28f5c28fp-2, 0x1.999999999999ap-7, 0x1.0a3d70a3d70a4p-6, 0x1.47ae147ae147bp-6, 0x1.47ae147ae147bp-2, 0x1.51eb851eb851fp-1, 0x1.0147ae147ae14p+0, 0x1.028f5c28f5c29p+0, 12},
    {"HOLDOUT_SWITCH_B", {0x1.47ae147ae147bp-2, 0x1.47ae147ae147bp-1, 0x1.0cccccccccccdp+0}, 0x1.b4p+6, 0x1.7cp+6, 0x1.28f5c28f5c28fp-2, 0x1.5c28f5c28f5c3p-2, 0x1.999999999999ap-7, 0x1.0a3d70a3d70a4p-6, 0x1.47ae147ae147bp-6, 0x1.47ae147ae147bp-2, 0x1.51eb851eb851fp-1, 0x1.0147ae147ae14p+0, 0x1.028f5c28f5c29p+0, 13},
    {"HOLDOUT_LAGGED", {0x1.47ae147ae147bp-3, 0x1.0f5c28f5c28f6p-1, 0x1.2p+0}, 0x1.98p+6, 0x1.88p+6, 0x1.c28f5c28f5c29p-3, 0x1.147ae147ae148p-2, 0x1.999999999999ap-7, 0x1.0a3d70a3d70a4p-6, 0x1.47ae147ae147bp-6, 0x1.0cccccccccccdp-1, 0x1.51eb851eb851fp-1, 0x1.0147ae147ae14p+0, 0x1.028f5c28f5c29p+0, 14},
    {"HOLDOUT_NEG_RATE", {0x1.47ae147ae147bp-2, 0x1.47ae147ae147bp-1, 0x1.0cccccccccccdp+0}, 0x1.98p+6, 0x1.88p+6, 0x1.c28f5c28f5c29p-3, 0x1.147ae147ae148p-2, 0x1.999999999999ap-7, -0x1.47ae147ae147bp-6, -0x1.999999999999ap-5, -0x1.0cccccccccccdp-1, 0x1.51eb851eb851fp-1, 0x1.0147ae147ae14p+0, 0x1.028f5c28f5c29p+0, 15},
};

static std::array<mvn_program_case_t, MVN_SELECTION_PRODUCT_CASES> selection_cases;
static std::array<mvn_program_case_t, MVN_HOLDOUT_PRODUCT_CASES> holdout_cases;
static std::array<std::array<mvn_component_case_t,
                            MVN_COMPONENT_CASES_PER_DIMENSION>, 3>
    selection_components;
static std::array<std::array<mvn_component_case_t,
                            MVN_COMPONENT_CASES_PER_DIMENSION>, 3>
    holdout_components;
static bool initialized;

struct Sha256 {
    std::uint32_t state[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
    };
    std::uint64_t bytes{};
    std::uint8_t block[64]{};
    unsigned used{};
};

static constexpr std::uint32_t SHA_K[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u,
};

static std::uint32_t rotate(std::uint32_t x, unsigned n) {
    return (x >> n) | (x << (32u - n));
}

static void sha_transform(Sha256 &s) {
    std::uint32_t w[64]{};
    for (unsigned i = 0; i < 16u; ++i) {
        w[i] = static_cast<std::uint32_t>(s.block[4u * i]) << 24u |
            static_cast<std::uint32_t>(s.block[4u * i + 1u]) << 16u |
            static_cast<std::uint32_t>(s.block[4u * i + 2u]) << 8u |
            static_cast<std::uint32_t>(s.block[4u * i + 3u]);
    }
    for (unsigned i = 16u; i < 64u; ++i) {
        const std::uint32_t a = rotate(w[i - 15u], 7u) ^
            rotate(w[i - 15u], 18u) ^ (w[i - 15u] >> 3u);
        const std::uint32_t b = rotate(w[i - 2u], 17u) ^
            rotate(w[i - 2u], 19u) ^ (w[i - 2u] >> 10u);
        w[i] = w[i - 16u] + a + w[i - 7u] + b;
    }
    std::uint32_t a=s.state[0], b=s.state[1], c=s.state[2], d=s.state[3];
    std::uint32_t e=s.state[4], f=s.state[5], g=s.state[6], h=s.state[7];
    for (unsigned i = 0; i < 64u; ++i) {
        const std::uint32_t s1 = rotate(e,6u)^rotate(e,11u)^rotate(e,25u);
        const std::uint32_t choice = (e&f)^(~e&g);
        const std::uint32_t t1 = h+s1+choice+SHA_K[i]+w[i];
        const std::uint32_t s0 = rotate(a,2u)^rotate(a,13u)^rotate(a,22u);
        const std::uint32_t majority = (a&b)^(a&c)^(b&c);
        const std::uint32_t t2 = s0+majority;
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    s.state[0]+=a; s.state[1]+=b; s.state[2]+=c; s.state[3]+=d;
    s.state[4]+=e; s.state[5]+=f; s.state[6]+=g; s.state[7]+=h;
}

static void sha_update(Sha256 &s, const void *data, std::size_t size) {
    const auto *byte = static_cast<const std::uint8_t *>(data);
    s.bytes += size;
    while (size != 0u) {
        const unsigned take = std::min<unsigned>(64u - s.used,
                                                 static_cast<unsigned>(size));
        std::memcpy(s.block + s.used, byte, take);
        s.used += take; byte += take; size -= take;
        if (s.used == 64u) {
            sha_transform(s);
            s.used = 0u;
        }
    }
}

static std::array<std::uint8_t,32> sha_finish(Sha256 s) {
    const std::uint64_t bits_count = s.bytes * 8u;
    const std::uint8_t one = 0x80u;
    sha_update(s, &one, 1u);
    const std::uint8_t zero = 0u;
    while (s.used != 56u)
        sha_update(s, &zero, 1u);
    std::uint8_t length[8]{};
    for (unsigned i = 0; i < 8u; ++i)
        length[7u-i] = static_cast<std::uint8_t>(bits_count >> (8u*i));
    sha_update(s, length, 8u);
    std::array<std::uint8_t,32> out{};
    for (unsigned i = 0; i < 8u; ++i) {
        out[4u*i] = static_cast<std::uint8_t>(s.state[i] >> 24u);
        out[4u*i+1u] = static_cast<std::uint8_t>(s.state[i] >> 16u);
        out[4u*i+2u] = static_cast<std::uint8_t>(s.state[i] >> 8u);
        out[4u*i+3u] = static_cast<std::uint8_t>(s.state[i]);
    }
    return out;
}

static mvn_program_case_t base_case(const char *name) {
    mvn_program_case_t c{};
    c.name = name;
    c.market.spot[0] = 100.0;
    c.market.spot[1] = 100.0;
    c.market.volatility[0] = 0.20;
    c.market.volatility[1] = 0.20;
    c.market.dividend[0] = 0.01;
    c.market.dividend[1] = 0.01;
    c.market.rate = 0.03;
    c.market.correlation = 0.50;
    c.contract.notional = 100.0;
    for (unsigned d = 0; d < 3; ++d) {
        c.market.observation_time[d] = static_cast<double>(d + 1u) / 3.0;
        c.contract.call_barrier[d][0] = c.market.spot[0] * (1.10 - 0.05 * d);
        c.contract.call_barrier[d][1] = c.market.spot[1] * (1.10 - 0.05 * d);
        c.contract.coupon_barrier[d][0] = c.market.spot[0] * 0.80;
        c.contract.coupon_barrier[d][1] = c.market.spot[1] * 0.80;
        c.contract.coupon_cashflow[d] = static_cast<double>(d + 1u);
        c.contract.call_redemption[d] = 100.0 + static_cast<double>(d);
        c.contract.coupon_payment_time[d] = c.market.observation_time[d];
        c.contract.call_payment_time[d] = c.market.observation_time[d];
    }
    c.contract.normalized_protection_barrier[0] = 0.65;
    c.contract.normalized_protection_barrier[1] = 0.65;
    c.contract.terminal_payment_time = 1.0;
    return c;
}

static mvn_program_case_t convert_parent(const autocall_worstof_case_t &p) {
    mvn_program_case_t c = base_case(p.name);
    c.stress = static_cast<std::uint32_t>(p.stress != 0);
    c.market.spot[0] = p.contract.spot_a;
    c.market.spot[1] = p.contract.spot_b;
    c.market.volatility[0] = p.market.sigma_a;
    c.market.volatility[1] = p.market.sigma_b;
    c.market.dividend[0] = p.market.dividend_a;
    c.market.dividend[1] = p.market.dividend_b;
    c.market.rate = p.market.rate;
    c.market.correlation = p.market.rho;
    c.contract.notional = p.contract.notional;
    for (unsigned d = 0; d < 3; ++d) {
        c.market.observation_time[d] = p.market.maturity * (d + 1.0) / 3.0;
        c.contract.call_barrier[d][0] =
            p.contract.spot_a * p.contract.call_barrier[d];
        c.contract.call_barrier[d][1] =
            p.contract.spot_b * p.contract.call_barrier[d];
        c.contract.coupon_barrier[d][0] =
            p.contract.spot_a * p.contract.coupon_barrier[d];
        c.contract.coupon_barrier[d][1] =
            p.contract.spot_b * p.contract.coupon_barrier[d];
        c.contract.coupon_cashflow[d] = p.contract.coupon_cashflow[d];
        c.contract.call_redemption[d] = p.contract.call_redemption[d];
        c.contract.coupon_payment_time[d] = p.contract.coupon_payment_time[d];
        c.contract.call_payment_time[d] = p.contract.call_payment_time[d];
    }
    c.contract.normalized_protection_barrier[0] = p.contract.protection_barrier;
    c.contract.normalized_protection_barrier[1] = p.contract.protection_barrier;
    c.contract.terminal_payment_time = p.contract.terminal_payment_time;
    return c;
}

static void materialize_extra(const ExtraSpec &s, mvn_program_case_t &c,
                              bool holdout) {
    c = base_case(s.name);
    c.stress = s.style >= 8u ? 1u : 0u;
    c.market.spot[0] = s.spot_a;
    c.market.spot[1] = s.spot_b;
    c.market.volatility[0] = s.sigma_a;
    c.market.volatility[1] = s.sigma_b;
    c.market.dividend[0] = s.q_a;
    c.market.dividend[1] = s.q_b;
    c.market.rate = s.rate;
    c.market.correlation = s.rho;
    for (unsigned d = 0; d < 3; ++d) {
        c.market.observation_time[d] = s.t[d];
        c.contract.coupon_payment_time[d] = s.t[d] +
            ((s.style == 14u) ? 0x1.47ae147ae147bp-6 : 0.0);
        c.contract.call_payment_time[d] = s.t[d] +
            ((s.style == 14u) ? 0x1.47ae147ae147bp-6 : 0.0);
    }
    c.contract.terminal_payment_time = s.t[2] +
        ((s.style == 14u) ? 0x1.47ae147ae147bp-6 : 0.0);
    c.contract.normalized_protection_barrier[0] = s.protection;
    c.contract.normalized_protection_barrier[1] = s.protection;
    for (unsigned d = 0; d < 3; ++d) {
        const double call_ratio = 1.10 - 0.05 * d;
        c.contract.call_barrier[d][0] = s.spot_a * call_ratio;
        c.contract.call_barrier[d][1] = s.spot_b * call_ratio;
        c.contract.coupon_barrier[d][0] = s.spot_a * 0.80;
        c.contract.coupon_barrier[d][1] = s.spot_b * 0.80;
    }
    c.contract.call_barrier[2][0] = s.spot_a * s.call3_a;
    c.contract.call_barrier[2][1] = s.spot_b * s.call3_b;
    if (s.style == 7u) {
        c.contract.call_barrier[0][0] = s.spot_a * (holdout ? 1.17 : 1.15);
        c.contract.call_barrier[0][1] = s.spot_b * (holdout ? 1.07 : 1.05);
        c.contract.coupon_barrier[1][0] = s.spot_a * (holdout ? 0.83 : 0.85);
        c.contract.coupon_barrier[1][1] = s.spot_b * (holdout ? 0.93 : 0.95);
    } else if (s.style == 8u) {
        for (unsigned d = 0; d < 3; ++d) {
            c.contract.call_barrier[d][0] =
                std::max(c.contract.call_barrier[d][0], s.spot_a * 1.60);
            c.contract.call_barrier[d][1] =
                std::max(c.contract.call_barrier[d][1], s.spot_b * 1.60);
            c.contract.coupon_barrier[d][0] = s.spot_a * 0.55;
            c.contract.coupon_barrier[d][1] = s.spot_b * 0.55;
        }
    } else if (s.style == 9u) {
        for (unsigned d = 0; d < 3; ++d) {
            c.contract.call_barrier[d][0] = s.spot_a * 1.50;
            c.contract.call_barrier[d][1] = s.spot_b * 1.50;
            c.contract.coupon_barrier[d][0] = s.spot_a * 1.45;
            c.contract.coupon_barrier[d][1] = s.spot_b * 1.45;
        }
    } else if (s.style == 10u || s.style == 11u) {
        for (unsigned d = 0; d < 3; ++d) {
            c.contract.coupon_barrier[d][0] =
                std::nextafter(c.contract.call_barrier[d][0], 0.0);
            c.contract.coupon_barrier[d][1] =
                std::nextafter(c.contract.call_barrier[d][1], 0.0);
        }
    }
}

static double regime_rho(unsigned regime, unsigned ordinal, bool holdout) {
    const double central[4] = {-0.50, 0.0, 0.50, 0.90};
    if (regime == MVN_CORPUS_INDEPENDENCE)
        return 0.0;
    if (regime == MVN_CORPUS_NEAR_POSITIVE_SINGULAR)
        return 1.0 - std::ldexp(1.0, -(holdout ? 30 : 24) - (ordinal & 3u));
    if (regime == MVN_CORPUS_NEAR_NEGATIVE_SINGULAR)
        return -1.0 + std::ldexp(1.0, -(holdout ? 30 : 24) - (ordinal & 3u));
    return central[(ordinal + (holdout ? 1u : 0u)) & 3u];
}

static void fill_component(mvn_component_case_t &out, unsigned dimension,
                           unsigned ordinal, bool holdout) {
    const unsigned regime = ordinal / 16u;
    const unsigned local = ordinal & 15u;
    const unsigned dates = dimension / 2u;
    const double times[3] = {0.25, 0.60, 1.10};
    const double rho = regime_rho(regime, local, holdout);
    out.id = (holdout ? 0x80000000u : 0u) | (dimension << 16u) | ordinal;
    out.dimension = static_cast<std::uint8_t>(dimension);
    out.regime = static_cast<std::uint8_t>(regime);
    out.finite_rectangle = static_cast<std::uint8_t>(
        regime == MVN_CORPUS_NARROW || regime == MVN_CORPUS_CANCELLATION);
    for (unsigned i = 0; i < dimension; ++i) {
        const unsigned di = i / 2u;
        const unsigned ai = i & 1u;
        for (unsigned j = 0; j < dimension; ++j) {
            const unsigned dj = j / 2u;
            const unsigned aj = j & 1u;
            const double time_corr = std::min(times[di], times[dj]) /
                std::sqrt(times[di] * times[dj]);
            out.correlation[i * 6u + j] = time_corr *
                (ai == aj ? 1.0 : rho);
        }
        double center = (static_cast<int>((local + i) % 7u) - 3) * 0.375;
        if (holdout)
            center += ((i + local) & 1u) ? 0.0625 : -0.0625;
        if (regime == MVN_CORPUS_LOWER_TAIL)
            center -= 5.5;
        if (regime == MVN_CORPUS_UPPER_TAIL)
            center += 5.5;
        out.lower[i] = -std::numeric_limits<double>::infinity();
        out.upper[i] = center;
        if (out.finite_rectangle) {
            const double width = regime == MVN_CORPUS_NARROW ?
                std::ldexp(1.0, -8 - static_cast<int>(local & 3u)) : 0.125;
            out.lower[i] = center - width;
            out.upper[i] = center + width;
        }
    }
    (void)dates;
}

static void initialize(void) {
    if (initialized)
        return;
    const autocall_worstof_case_t *parent = autocall_worstof_cases();
    for (unsigned i = 0; i < 38u; ++i)
        selection_cases[i] = convert_parent(parent[i]);
    for (unsigned i = 0; i < 16u; ++i) {
        materialize_extra(SELECTION_EXTRA[i], selection_cases[38u + i], false);
        materialize_extra(HOLDOUT_EXTRA[i], holdout_cases[i], true);
    }
    for (unsigned d = 0; d < 3u; ++d) {
        const unsigned dimension = 2u * (d + 1u);
        for (unsigned i = 0; i < MVN_COMPONENT_CASES_PER_DIMENSION; ++i) {
            fill_component(selection_components[d][i], dimension, i, false);
            fill_component(holdout_components[d][i], dimension, i, true);
        }
    }
    initialized = true;
}

} // namespace

extern "C" const mvn_program_case_t *mvn_selection_product_cases(size_t *count) {
    initialize();
    if (count != nullptr)
        *count = selection_cases.size();
    return selection_cases.data();
}

extern "C" const mvn_program_case_t *mvn_holdout_product_cases(size_t *count) {
    initialize();
    if (count != nullptr)
        *count = holdout_cases.size();
    return holdout_cases.data();
}

static const mvn_component_case_t *component_cases(unsigned dimension,
                                                    size_t *count,
                                                    bool holdout) {
    initialize();
    if (dimension != 2u && dimension != 4u && dimension != 6u) {
        if (count != nullptr)
            *count = 0u;
        return nullptr;
    }
    if (count != nullptr)
        *count = MVN_COMPONENT_CASES_PER_DIMENSION;
    const unsigned slot = dimension / 2u - 1u;
    return holdout ? holdout_components[slot].data() :
                     selection_components[slot].data();
}

extern "C" const mvn_component_case_t *mvn_selection_component_cases(
    unsigned dimension, size_t *count) {
    return component_cases(dimension, count, false);
}

extern "C" const mvn_component_case_t *mvn_holdout_component_cases(
    unsigned dimension, size_t *count) {
    return component_cases(dimension, count, true);
}

static void hash_u64(Sha256 &sha, std::uint64_t value) {
    std::uint8_t byte[8]{};
    for (unsigned i = 0; i < 8u; ++i)
        byte[7u - i] = static_cast<std::uint8_t>(value >> (8u * i));
    sha_update(sha, byte, sizeof(byte));
}

static void hash_double(Sha256 &sha, double value) {
    hash_u64(sha, std::bit_cast<std::uint64_t>(value));
}

static void hash_case(Sha256 &sha, const mvn_program_case_t &c) {
    const std::size_t name_size = std::strlen(c.name);
    hash_u64(sha, name_size);
    sha_update(sha, c.name, name_size);
    hash_u64(sha, c.stress);
    hash_u64(sha, c.structural);
    for (double x : c.market.spot) hash_double(sha, x);
    for (double x : c.market.volatility) hash_double(sha, x);
    for (double x : c.market.dividend) hash_double(sha, x);
    hash_double(sha, c.market.rate);
    hash_double(sha, c.market.correlation);
    for (double x : c.market.observation_time) hash_double(sha, x);
    hash_double(sha, c.contract.notional);
    for (const auto &date : c.contract.call_barrier)
        for (double x : date) hash_double(sha, x);
    for (const auto &date : c.contract.coupon_barrier)
        for (double x : date) hash_double(sha, x);
    for (double x : c.contract.coupon_cashflow) hash_double(sha, x);
    for (double x : c.contract.call_redemption) hash_double(sha, x);
    for (double x : c.contract.coupon_payment_time) hash_double(sha, x);
    for (double x : c.contract.call_payment_time) hash_double(sha, x);
    for (double x : c.contract.normalized_protection_barrier) hash_double(sha, x);
    hash_double(sha, c.contract.terminal_payment_time);
}

static void hash_component(Sha256 &sha, const mvn_component_case_t &c) {
    hash_u64(sha, c.id);
    hash_u64(sha, c.dimension);
    hash_u64(sha, c.regime);
    hash_u64(sha, c.finite_rectangle);
    for (double x : c.lower) hash_double(sha, x);
    for (double x : c.upper) hash_double(sha, x);
    for (double x : c.correlation) hash_double(sha, x);
}

static std::string digest_manifest(bool holdout) {
    initialize();
    Sha256 sha;
    if (holdout) {
        for (const auto &c : holdout_cases) hash_case(sha, c);
        for (const auto &dimension : holdout_components)
            for (const auto &c : dimension) hash_component(sha, c);
    } else {
        for (const auto &c : selection_cases) hash_case(sha, c);
        for (const auto &dimension : selection_components)
            for (const auto &c : dimension) hash_component(sha, c);
    }
    const auto bytes = sha_finish(sha);
    static constexpr char HEX[] = "0123456789abcdef";
    std::string out(64u, '0');
    for (unsigned i = 0; i < bytes.size(); ++i) {
        out[2u*i] = HEX[bytes[i] >> 4u];
        out[2u*i+1u] = HEX[bytes[i] & 15u];
    }
    return out;
}

extern "C" const char *mvn_selection_manifest_sha256(void) {
    static const std::string digest = digest_manifest(false);
    return digest.c_str();
}

extern "C" const char *mvn_holdout_manifest_sha256(void) {
    static const std::string digest = digest_manifest(true);
    return digest.c_str();
}

extern "C" int mvn_verify_manifest_integrity(void) {
    initialize();
    static constexpr const char *EXPECTED_SELECTION =
        "55928c22d6ff9d1f72277122afeac47ea13176745fd389224b8ec3c7a30c0e09";
    static constexpr const char *EXPECTED_HOLDOUT =
        "6b170fd9b2b172b6948002d22686a089d33b0d20f5a749cb08ae51b6a1a9826f";
    const bool counts = selection_cases.size() == MVN_SELECTION_PRODUCT_CASES &&
        holdout_cases.size() == MVN_HOLDOUT_PRODUCT_CASES &&
        selection_components[0].size() == MVN_COMPONENT_CASES_PER_DIMENSION &&
        std::strcmp(mvn_selection_manifest_sha256(), EXPECTED_SELECTION) == 0 &&
        std::strcmp(mvn_holdout_manifest_sha256(), EXPECTED_HOLDOUT) == 0;
    std::printf("MANIFEST_INTEGRITY selection_product=%zu holdout_product=%zu "
                "component_per_dimension=%zu selection_sha256=%s holdout_sha256=%s "
                "status=%s\n",
                selection_cases.size(), holdout_cases.size(),
                selection_components[0].size(), mvn_selection_manifest_sha256(),
                mvn_holdout_manifest_sha256(), counts ? "PASS" : "FAIL");
    return counts ? 1 : 0;
}
