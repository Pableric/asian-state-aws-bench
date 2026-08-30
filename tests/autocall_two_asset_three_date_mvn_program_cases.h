#ifndef AUTOCALL_TWO_ASSET_THREE_DATE_MVN_PROGRAM_CASES_H
#define AUTOCALL_TWO_ASSET_THREE_DATE_MVN_PROGRAM_CASES_H

#include "private/autocall_two_asset_three_date_mvn_program_diag.h"

#include <stddef.h>
#include <stdint.h>

enum {
    MVN_SELECTION_PRODUCT_CASES = 54,
    MVN_HOLDOUT_PRODUCT_CASES = 16,
    MVN_COMPONENT_CASES_PER_DIMENSION = 128,
};

typedef enum {
    MVN_CORPUS_CENTRAL = 0,
    MVN_CORPUS_NARROW = 1,
    MVN_CORPUS_LOWER_TAIL = 2,
    MVN_CORPUS_UPPER_TAIL = 3,
    MVN_CORPUS_CANCELLATION = 4,
    MVN_CORPUS_INDEPENDENCE = 5,
    MVN_CORPUS_NEAR_POSITIVE_SINGULAR = 6,
    MVN_CORPUS_NEAR_NEGATIVE_SINGULAR = 7,
} mvn_component_regime_t;

typedef struct {
    uint32_t id;
    uint8_t dimension;
    uint8_t regime;
    uint8_t finite_rectangle;
    uint8_t reserved;
    double lower[6];
    double upper[6];
    double correlation[36];
} mvn_component_case_t;

#ifdef __cplusplus
extern "C" {
#endif

const mvn_program_case_t *mvn_selection_product_cases(size_t *count);
const mvn_program_case_t *mvn_holdout_product_cases(size_t *count);
const mvn_component_case_t *mvn_selection_component_cases(unsigned dimension,
                                                           size_t *count);
const mvn_component_case_t *mvn_holdout_component_cases(unsigned dimension,
                                                         size_t *count);
const char *mvn_selection_manifest_sha256(void);
const char *mvn_holdout_manifest_sha256(void);
int mvn_verify_manifest_integrity(void);

#ifdef __cplusplus
}
#endif

#endif
