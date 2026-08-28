#ifndef AUTOCALL_TWO_ASSET_THREE_DATE_WORSTOF_CASES_H
#define AUTOCALL_TWO_ASSET_THREE_DATE_WORSTOF_CASES_H

#include "private/autocall_two_asset_three_date_worstof_raw_diag.h"

typedef struct {
    const char *name;
    int stress;
    autocall_worstof_market_input_t market;
    autocall_worstof_contract_input_t contract;
} autocall_worstof_case_t;

enum {
    AUTOCALL_WORSTOF_CORE_CASES = 10,
    AUTOCALL_WORSTOF_STRESS_CASES = 28,
    AUTOCALL_WORSTOF_CASES = 38,
};

#ifdef __cplusplus
extern "C" {
#endif

const autocall_worstof_case_t *autocall_worstof_cases(void);
int autocall_worstof_native_case_index(unsigned ordinal);

#ifdef __cplusplus
}
#endif

#endif
