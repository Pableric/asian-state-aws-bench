#include "european_pricer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void usage(const char* program) {
    fprintf(stderr,
        "usage: %s points [call|put] [s0 k r sigma t] [repeats]\n",
        program);
}

static double monotonic_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1.0e-9;
}

int main(int argc, char** argv) {
    if (argc != 2 && argc != 3 && argc != 8 && argc != 9) {
        usage(argv[0]);
        return 2;
    }
    const uint64_t points = strtoull(argv[1], NULL, 10);
    european_price_request_t req = {
        .s0 = 100.0f,
        .k = 100.0f,
        .r = 0.05f,
        .sigma = 0.2f,
        .t = 1.0f,
        .num_blocks = 0,
        .type = EUROPEAN_CALL,
        .mode = EUROPEAN_MODE_ORDERED_D1_MIN_FMA,
    };
    if (argc >= 3) {
        if (strcmp(argv[2], "put") == 0) {
            req.type = EUROPEAN_PUT;
        } else if (strcmp(argv[2], "call") != 0) {
            usage(argv[0]);
            return 2;
        }
    }
    if (argc == 8 || argc == 9) {
        req.s0 = strtof(argv[3], NULL);
        req.k = strtof(argv[4], NULL);
        req.r = strtof(argv[5], NULL);
        req.sigma = strtof(argv[6], NULL);
        req.t = strtof(argv[7], NULL);
    }
    const unsigned long repeats = argc == 9 ? strtoul(argv[8], NULL, 10) : 1UL;
    if (repeats == 0) {
        usage(argv[0]);
        return 2;
    }

    european_price_result_t result;
    const double wall_start = monotonic_seconds();
    for (unsigned long repeat = 0; repeat < repeats; ++repeat) {
        const int rc = price_european_points(&req, points, &result);
        if (rc != 0) {
            fprintf(stderr, "pricing failed: rc=%d\n", rc);
            return 1;
        }
    }
    const double wall_seconds = monotonic_seconds() - wall_start;
    european_price_request_t analytic_req = req;
    analytic_req.num_blocks = 1;
    const double analytic = black_scholes_price(&analytic_req);
    const double absolute_error = result.price > analytic ?
        result.price - analytic : analytic - result.price;
    printf(
        "RESULT points=%llu samples=%llu type=%s mode=ordered-d1-min-fma "
        "price=%.12g analytic=%.12g abs_err=%.6g coeff_setup_seconds=%.9f "
        "kernel_seconds=%.9f ns_per_sample=%.6f repeats=%lu wall_seconds=%.9f "
        "wall_samples_per_sec=%.6f\n",
        (unsigned long long)points,
        (unsigned long long)result.samples,
        req.type == EUROPEAN_CALL ? "call" : "put",
        result.price,
        analytic,
        absolute_error,
        result.coeff_setup_seconds,
        result.kernel_seconds,
        result.kernel_seconds * 1.0e9 / (double)result.samples,
        repeats,
        wall_seconds,
        (double)result.samples * (double)repeats / wall_seconds);
    return 0;
}
