#ifndef ASIAN_QUANTLIB_ACCURACY_CASES_H
#define ASIAN_QUANTLIB_ACCURACY_CASES_H

#include <stddef.h>
#include <stdint.h>

enum {
    ASIAN_ACCURACY_PATHS = 4096,
    ASIAN_ACCURACY_PUBLISHED_CASES = 24,
    ASIAN_ACCURACY_REFERENCE_REPLICATIONS = 16,
};

typedef struct {
    const char *id;
    uint32_t first_twelfths;
    uint32_t fixings;
    double s0;
    double strike;
    double dividend_yield;
    double rate;
    double sigma;
    double published_price;
} asian_accuracy_published_case_t;

/* Levy (1997), as frozen in QuantLib's Asian-option regression suite. */
static const asian_accuracy_published_case_t
asian_accuracy_published_cases[ASIAN_ACCURACY_PUBLISHED_CASES] = {
    {"levy_f0_n2",   0u,   2u, 90.0, 87.0, 0.06, 0.025, 0.13, 1.3942835683},
    {"levy_f0_n4",   0u,   4u, 90.0, 87.0, 0.06, 0.025, 0.13, 1.5852442983},
    {"levy_f0_n8",   0u,   8u, 90.0, 87.0, 0.06, 0.025, 0.13, 1.6697067300},
    {"levy_f0_n12",  0u,  12u, 90.0, 87.0, 0.06, 0.025, 0.13, 1.6980019214},
    {"levy_f0_n26",  0u,  26u, 90.0, 87.0, 0.06, 0.025, 0.13, 1.7255070456},
    {"levy_f0_n52",  0u,  52u, 90.0, 87.0, 0.06, 0.025, 0.13, 1.7401553533},
    {"levy_f0_n100", 0u, 100u, 90.0, 87.0, 0.06, 0.025, 0.13, 1.7478303712},
    {"levy_f0_n250", 0u, 250u, 90.0, 87.0, 0.06, 0.025, 0.13, 1.7490291943},

    {"levy_f1_n2",   1u,   2u, 90.0, 87.0, 0.06, 0.025, 0.13, 1.8496053697},
    {"levy_f1_n4",   1u,   4u, 90.0, 87.0, 0.06, 0.025, 0.13, 2.0111495205},
    {"levy_f1_n8",   1u,   8u, 90.0, 87.0, 0.06, 0.025, 0.13, 2.0852138818},
    {"levy_f1_n12",  1u,  12u, 90.0, 87.0, 0.06, 0.025, 0.13, 2.1105094397},
    {"levy_f1_n26",  1u,  26u, 90.0, 87.0, 0.06, 0.025, 0.13, 2.1346526695},
    {"levy_f1_n52",  1u,  52u, 90.0, 87.0, 0.06, 0.025, 0.13, 2.1474896510},
    {"levy_f1_n100", 1u, 100u, 90.0, 87.0, 0.06, 0.025, 0.13, 2.1547281090},
    {"levy_f1_n250", 1u, 250u, 90.0, 87.0, 0.06, 0.025, 0.13, 2.1564276565},

    {"levy_f3_n2",   3u,   2u, 90.0, 87.0, 0.06, 0.025, 0.13, 2.63315092584},
    {"levy_f3_n4",   3u,   4u, 90.0, 87.0, 0.06, 0.025, 0.13, 2.76723962361},
    {"levy_f3_n8",   3u,   8u, 90.0, 87.0, 0.06, 0.025, 0.13, 2.83124836881},
    {"levy_f3_n12",  3u,  12u, 90.0, 87.0, 0.06, 0.025, 0.13, 2.84290301412},
    {"levy_f3_n26",  3u,  26u, 90.0, 87.0, 0.06, 0.025, 0.13, 2.88179560417},
    {"levy_f3_n52",  3u,  52u, 90.0, 87.0, 0.06, 0.025, 0.13, 2.88447044543},
    {"levy_f3_n100", 3u, 100u, 90.0, 87.0, 0.06, 0.025, 0.13, 2.89985329603},
    {"levy_f3_n250", 3u, 250u, 90.0, 87.0, 0.06, 0.025, 0.13, 2.90047296063},
};

static inline uint32_t asian_accuracy_schedule_basis(uint32_t fixings)
{
    return 12u * (fixings - 1u);
}

static inline uint32_t asian_accuracy_schedule_first_tick(
    const asian_accuracy_published_case_t *c)
{
    return c->first_twelfths * (c->fixings - 1u);
}

static inline int asian_accuracy_schedule_supported(
    const asian_accuracy_published_case_t *c)
{
    return c->first_twelfths == 0u ||
           (c->first_twelfths == 1u && c->fixings == 12u);
}

static inline const char *asian_accuracy_schedule_reason(
    const asian_accuracy_published_case_t *c)
{
    if (c->first_twelfths == 1u)
        return "future_grid_mismatch";
    return "delayed_first_fixing_not_representable";
}

static inline uint64_t asian_accuracy_splitmix64(uint64_t value)
{
    value += UINT64_C(0x9e3779b97f4a7c15);
    value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31);
}

static inline uint32_t asian_accuracy_reference_seed(uint32_t level,
                                                      uint32_t replication)
{
    const uint64_t key = UINT64_C(0xa51aacc0c0ffee01) ^
        ((uint64_t)level << 32) ^ replication;
    uint32_t seed = (uint32_t)asian_accuracy_splitmix64(key);
    return seed == 0u ? 1u : seed;
}

#endif
