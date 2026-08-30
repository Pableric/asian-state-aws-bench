#ifndef AUTOCALL_TWO_ASSET_THREE_DATE_MVN_PROGRAM_DIAG_H
#define AUTOCALL_TWO_ASSET_THREE_DATE_MVN_PROGRAM_DIAG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    MVN_PROGRAM_DATES = 3,
    MVN_PROGRAM_ASSETS = 2,
    MVN_PROGRAM_SCENARIOS = 9,
    MVN_PROGRAM_LOGICAL_NODES = 26,
    MVN_PROGRAM_RISK_TASKS = 234,
    MVN_PROGRAM_MAX_TEMPLATES = 13,
    MVN_PROGRAM_MAX_UNIQUE_NODES = MVN_PROGRAM_RISK_TASKS,
    MVN_PROGRAM_MAX_TILT_FAMILIES = 45,
    MVN_PROGRAM_MAX_LOWERED_BATCHES = 64,
};

typedef enum {
    MVN_PROGRAM_OK = 0,
    MVN_PROGRAM_INVALID_INPUT = 1,
    UNSUPPORTED_MVN_TOPOLOGY = 2,
    MVN_PROGRAM_UNSUPPORTED_TOPOLOGY = UNSUPPORTED_MVN_TOPOLOGY,
    MVN_PROGRAM_UNCERTIFIED = 3,
    MVN_PROGRAM_NATIVE_MATH_FAILED = 4,
} mvn_program_status_t;

typedef enum {
    MVN_SCENARIO_BASE = 0,
    MVN_SCENARIO_SPOT_A_MINUS = 1,
    MVN_SCENARIO_SPOT_A_PLUS = 2,
    MVN_SCENARIO_SPOT_B_MINUS = 3,
    MVN_SCENARIO_SPOT_B_PLUS = 4,
    MVN_SCENARIO_VOL_A_MINUS = 5,
    MVN_SCENARIO_VOL_A_PLUS = 6,
    MVN_SCENARIO_VOL_B_MINUS = 7,
    MVN_SCENARIO_VOL_B_PLUS = 8,
} mvn_program_scenario_t;

typedef enum {
    MVN_NODE_CALL = 0,
    MVN_NODE_COUPON = 1,
    MVN_NODE_PROTECTED = 2,
    MVN_NODE_DOWNSIDE_A = 3,
    MVN_NODE_DOWNSIDE_B = 4,
} mvn_probability_node_kind_t;

typedef enum {
    MVN_TEMPLATE_ORDINARY = 0,
    MVN_TEMPLATE_DOWNSIDE = 1,
    MVN_TEMPLATE_SINGULAR_REDUCED = 2,
} mvn_template_family_t;

typedef struct {
    double spot[MVN_PROGRAM_ASSETS];
    double volatility[MVN_PROGRAM_ASSETS];
    double dividend[MVN_PROGRAM_ASSETS];
    double rate;
    double correlation;
    double observation_time[MVN_PROGRAM_DATES];
} mvn_program_market_input_t;

typedef struct {
    double notional;
    /* Absolute spot barriers; normalized values are prepared cold. */
    double call_barrier[MVN_PROGRAM_DATES][MVN_PROGRAM_ASSETS];
    double coupon_barrier[MVN_PROGRAM_DATES][MVN_PROGRAM_ASSETS];
    double coupon_cashflow[MVN_PROGRAM_DATES];
    double call_redemption[MVN_PROGRAM_DATES];
    double coupon_payment_time[MVN_PROGRAM_DATES];
    double call_payment_time[MVN_PROGRAM_DATES];
    /* Both normalized entries must have identical binary64 bits. */
    double normalized_protection_barrier[MVN_PROGRAM_ASSETS];
    double terminal_payment_time;
} mvn_program_contract_input_t;

typedef struct {
    const char *name;
    uint32_t stress;
    uint32_t structural;
    mvn_program_market_input_t market;
    mvn_program_contract_input_t contract;
} mvn_program_case_t;

typedef struct {
    uint16_t id;
    uint8_t dimension;
    uint8_t family;
    uint8_t variable_count;
    uint8_t regime;
    uint8_t kernel;
    uint8_t accuracy_tier;
    uint8_t variable_date[6];
    uint8_t variable_asset[6];
    uint8_t transform_id;
    uint8_t reserved[7];
    double covariance[36];
    double factor[36];
    double conditional_variance[6];
    double partial_correlation[15];
    uint64_t identity;
    uint64_t generation;
} __attribute__((aligned(64))) mvn_covariance_template_t;

typedef struct {
    uint16_t template_id;
    uint8_t scenario;
    uint8_t accuracy_tier;
    uint8_t dimension;
    uint8_t reserved[3];
    double lower[6];
    double upper[6];
    double mean_shift[6];
    uint64_t identity;
} __attribute__((aligned(64))) mvn_bound_instance_t;

typedef struct {
    uint16_t id;
    uint8_t selected_asset;
    uint8_t scenario;
    uint16_t template_id;
    uint16_t reserved;
    double exponential_prefactor;
    double mean_shift[6];
    uint64_t identity;
} __attribute__((aligned(64))) mvn_tilt_family_t;

typedef struct {
    uint16_t bound_id;
    uint16_t tilt_id;
    uint8_t kind;
    uint8_t dimension;
    int8_t inclusion_sign;
    uint8_t scenario;
    double coefficient;
    double certified_abs_error;
    uint64_t identity;
} __attribute__((aligned(32))) mvn_probability_node_t;

typedef struct {
    uint16_t first_node;
    uint16_t node_count;
    uint16_t positive_count;
    uint16_t negative_count;
    double constant;
    uint64_t identity;
} __attribute__((aligned(32))) mvn_linear_combination_node_t;

typedef struct {
    uint16_t first_task;
    uint16_t task_count;
    uint16_t template_id;
    uint8_t dimension;
    uint8_t kernel;
    uint8_t regime;
    uint8_t tilt_family;
    uint8_t accuracy_tier;
    uint8_t reserved[5];
    uint64_t identity;
} __attribute__((aligned(32))) mvn_lowered_batch_t;

typedef struct {
    uint16_t task_count;
    uint16_t batch_count;
    uint32_t scenario_mask;
    double lower[MVN_PROGRAM_RISK_TASKS][6];
    double upper[MVN_PROGRAM_RISK_TASKS][6];
    double coefficient[MVN_PROGRAM_RISK_TASKS];
    uint16_t bound_id[MVN_PROGRAM_RISK_TASKS];
    uint16_t node_id[MVN_PROGRAM_RISK_TASKS];
    uint64_t identity;
    uint64_t generation;
} __attribute__((aligned(64))) mvn_prepared_probability_batch_t;

typedef struct {
    mvn_covariance_template_t templates[MVN_PROGRAM_MAX_TEMPLATES];
    mvn_tilt_family_t tilts[MVN_PROGRAM_MAX_TILT_FAMILIES];
    mvn_probability_node_t nodes[MVN_PROGRAM_RISK_TASKS];
    mvn_linear_combination_node_t reductions[MVN_PROGRAM_SCENARIOS];
    mvn_lowered_batch_t schedule[MVN_PROGRAM_MAX_LOWERED_BATCHES];
    mvn_prepared_probability_batch_t tasks;
    uint16_t template_count;
    uint16_t tilt_count;
    uint16_t node_count;
    uint16_t schedule_count;
    uint64_t structure_identity;
    uint64_t market_identity;
    uint64_t generation;
    mvn_program_status_t status;
    uint32_t reserved;
} __attribute__((aligned(64))) mvn_prepared_gaussian_program_t;

typedef struct {
    double price[MVN_PROGRAM_SCENARIOS];
    double delta[MVN_PROGRAM_ASSETS];
    double gamma[MVN_PROGRAM_ASSETS];
    double vega_per_unit[MVN_PROGRAM_ASSETS];
    double vega_per_point[MVN_PROGRAM_ASSETS];
    double effective_spot[MVN_PROGRAM_SCENARIOS];
    double effective_volatility[MVN_PROGRAM_SCENARIOS];
    uint64_t program_identity;
    uint32_t fallback_count;
    uint32_t certified;
    mvn_program_status_t status;
    uint32_t reserved;
} __attribute__((aligned(64))) mvn_risk_scenario_output_t;

typedef struct {
    double price;
    double certified_abs_error;
    uint64_t program_identity;
    uint32_t fallback_count;
    uint32_t certified;
    mvn_program_status_t status;
    uint32_t reserved;
} __attribute__((aligned(64))) mvn_price_output_t;

#ifdef __cplusplus
}
#endif

#endif
