#include "asian_pricer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char* prog) {
    fprintf(stderr, "usage: %s blocks [call|put] [reference|final-z|rank1|coefficient-pair] [s0 k r sigma t]\n", prog);
}

int main(int argc, char** argv) {
    if (argc != 2 && argc != 3 && argc != 4 && argc != 9) {
        usage(argv[0]);
        return 2;
    }

    asian_price_request_t req;
    req.s0 = 100.0f;
    req.k = 100.0f;
    req.r = 0.05f;
    req.sigma = 0.2f;
    req.t = 1.0f;
    req.num_blocks = strtoull(argv[1], NULL, 10);
    req.type = ASIAN_CALL;
    req.mode = ASIAN_MODE_BUFFER_REFERENCE;

    if (argc >= 3) {
        if (strcmp(argv[2], "put") == 0) {
            req.type = ASIAN_PUT;
        } else if (strcmp(argv[2], "call") != 0) {
            usage(argv[0]);
            return 2;
        }
    }

    if (argc >= 4) {
        if (strcmp(argv[3], "final-z") == 0) req.mode = ASIAN_MODE_FINAL_Z;
        else if (strcmp(argv[3], "rank1") == 0) req.mode = ASIAN_MODE_RANK1;
        else if (strcmp(argv[3], "coefficient-pair") == 0) req.mode = ASIAN_MODE_COEFFICIENT_PAIR;
        else if (strcmp(argv[3], "reference") != 0) {
            usage(argv[0]);
            return 2;
        }
    }

    if (argc == 9) {
        req.s0 = strtof(argv[4], NULL);
        req.k = strtof(argv[5], NULL);
        req.r = strtof(argv[6], NULL);
        req.sigma = strtof(argv[7], NULL);
        req.t = strtof(argv[8], NULL);
    }

    asian_price_result_t result;
    const int rc = price_asian(&req, &result);
    if (rc != 0) {
        fprintf(stderr, "price_asian failed: rc=%d\n", rc);
        return 1;
    }

    const double samples_per_sec = (double)result.samples / result.kernel_seconds;
    const double ns_per_sample = result.kernel_seconds * 1.0e9 / (double)result.samples;

    const char* mode = asian_mode_name(req.mode);

    printf("RESULT blocks=%llu samples=%llu type=%s mode=%s experimental=%d price=%.12g coeff_setup_seconds=%.9f kernel_seconds=%.9f samples_per_sec=%.6f ns_per_sample=%.6f\n",
           (unsigned long long)req.num_blocks,
           (unsigned long long)result.samples,
           req.type == ASIAN_CALL ? "call" : "put",
           mode,
           asian_mode_is_experimental(req.mode),
           result.price,
           result.coeff_setup_seconds,
           result.kernel_seconds,
           samples_per_sec,
           ns_per_sample);

    return 0;
}
