#include "european_pricer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void usage(const char* prog) {
    fprintf(stderr, "usage: %s blocks [call|put] [buffer|gaussian-exp|gaussian-exp-reduced-fma|gaussian-exp-reduced-fma-prepared|gaussian-dynamic-ranges|gaussian-center-shared|gaussian-split-tail|direct|hybrid|hybrid-direct-tail] [s0 k r sigma t] [repeats]\n", prog);
}

static double monotonic_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1.0e-9;
}

int main(int argc, char** argv) {
    if (argc != 2 && argc != 3 && argc != 4 && argc != 5 && argc != 9 && argc != 10) {
        usage(argv[0]);
        return 2;
    }

    european_price_request_t req;
    req.s0 = 100.0f;
    req.k = 100.0f;
    req.r = 0.05f;
    req.sigma = 0.2f;
    req.t = 1.0f;
    req.num_blocks = strtoull(argv[1], NULL, 10);
    req.type = EUROPEAN_CALL;
    req.mode = EUROPEAN_MODE_BUFFER_REFERENCE;
    int prepared_mode = 0;

    if (argc >= 3) {
        if (strcmp(argv[2], "put") == 0) {
            req.type = EUROPEAN_PUT;
        } else if (strcmp(argv[2], "call") != 0) {
            usage(argv[0]);
            return 2;
        }
    }

    if (argc >= 4) {
        if (strcmp(argv[3], "gaussian-exp") == 0) {
            req.mode = EUROPEAN_MODE_GAUSSIAN_EXP;
        } else if (strcmp(argv[3], "gaussian-exp-reduced-fma") == 0) {
            req.mode = EUROPEAN_MODE_GAUSSIAN_EXP_REDUCED_FMA;
        } else if (strcmp(argv[3], "gaussian-exp-reduced-fma-prepared") == 0) {
            req.mode = EUROPEAN_MODE_GAUSSIAN_EXP_REDUCED_FMA;
            prepared_mode = 1;
        } else if (strcmp(argv[3], "gaussian-dynamic-ranges") == 0) {
            req.mode = EUROPEAN_MODE_GAUSSIAN_DYNAMIC_RANGES;
        } else if (strcmp(argv[3], "gaussian-center-shared") == 0) {
            req.mode = EUROPEAN_MODE_GAUSSIAN_CENTER_SHARED;
        } else if (strcmp(argv[3], "gaussian-split-tail") == 0) {
            req.mode = EUROPEAN_MODE_GAUSSIAN_SPLIT_TAIL;
        } else if (strcmp(argv[3], "direct") == 0) {
            req.mode = EUROPEAN_MODE_DIRECT_PAYOFF;
        } else if (strcmp(argv[3], "hybrid") == 0) {
            req.mode = EUROPEAN_MODE_HYBRID;
        } else if (strcmp(argv[3], "hybrid-direct-tail") == 0) {
            req.mode = EUROPEAN_MODE_HYBRID_DIRECT_TAIL;
        } else if (strcmp(argv[3], "buffer") != 0) {
            usage(argv[0]);
            return 2;
        }
    }

    if (argc == 9 || argc == 10) {
        req.s0 = strtof(argv[4], NULL);
        req.k = strtof(argv[5], NULL);
        req.r = strtof(argv[6], NULL);
        req.sigma = strtof(argv[7], NULL);
        req.t = strtof(argv[8], NULL);
    }

    const unsigned long repeats = (argc == 5) ? strtoul(argv[4], NULL, 10) :
                                  (argc == 10) ? strtoul(argv[9], NULL, 10) : 1UL;
    if (repeats == 0) {
        usage(argv[0]);
        return 2;
    }

    european_prepared_contract_t* prepared = NULL;
    double prepare_seconds = 0.0;
    if (prepared_mode) {
        const double prepare_start = monotonic_seconds();
        const int rc = european_prepare(&req, &prepared);
        prepare_seconds = monotonic_seconds() - prepare_start;
        if (rc != 0) {
            fprintf(stderr, "european_prepare failed: rc=%d\n", rc);
            return 1;
        }
    }

    european_price_result_t result;
    const double wall_start = monotonic_seconds();
    for (unsigned long repeat = 0; repeat < repeats; ++repeat) {
        const int rc = prepared_mode ?
            european_price_prepared(prepared, &result) :
            price_european(&req, &result);
        if (rc != 0) {
            fprintf(stderr, "pricing failed: rc=%d\n", rc);
            european_prepared_destroy(prepared);
            return 1;
        }
    }
    const double wall_seconds = monotonic_seconds() - wall_start;
    european_prepared_destroy(prepared);

    const double analytic = black_scholes_price(&req);
    const double samples_per_sec = (double)result.samples / result.kernel_seconds;
    const double ns_per_sample = result.kernel_seconds * 1.0e9 / (double)result.samples;

    const char* mode = prepared_mode ? "gaussian-exp-reduced-fma-prepared" :
                       req.mode == EUROPEAN_MODE_GAUSSIAN_EXP ? "gaussian-exp" :
                       req.mode == EUROPEAN_MODE_GAUSSIAN_EXP_REDUCED_FMA ? "gaussian-exp-reduced-fma" :
                       req.mode == EUROPEAN_MODE_GAUSSIAN_DYNAMIC_RANGES ? "gaussian-dynamic-ranges" :
                       req.mode == EUROPEAN_MODE_GAUSSIAN_CENTER_SHARED ? "gaussian-center-shared" :
                       req.mode == EUROPEAN_MODE_GAUSSIAN_SPLIT_TAIL ? "gaussian-split-tail" :
                       req.mode == EUROPEAN_MODE_DIRECT_PAYOFF ? "direct" :
                       req.mode == EUROPEAN_MODE_HYBRID ? "hybrid" :
                       req.mode == EUROPEAN_MODE_HYBRID_DIRECT_TAIL ? "hybrid-direct-tail" : "buffer";

    printf("RESULT blocks=%llu samples=%llu type=%s mode=%s price=%.12g analytic=%.12g abs_err=%.6g prepare_seconds=%.9f coeff_setup_seconds=%.9f kernel_seconds=%.9f samples_per_sec=%.6f ns_per_sample=%.6f repeats=%lu wall_seconds=%.9f wall_samples_per_sec=%.6f\n",
           (unsigned long long)req.num_blocks,
           (unsigned long long)result.samples,
           req.type == EUROPEAN_CALL ? "call" : "put",
           mode,
           result.price,
           analytic,
           result.price > analytic ? result.price - analytic : analytic - result.price,
           prepare_seconds,
           result.coeff_setup_seconds,
           result.kernel_seconds,
           samples_per_sec,
           ns_per_sample,
           repeats,
           wall_seconds,
           (double)result.samples * (double)repeats / wall_seconds);

    return 0;
}
