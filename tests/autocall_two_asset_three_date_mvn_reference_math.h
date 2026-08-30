#ifndef AUTOCALL_TWO_ASSET_THREE_DATE_MVN_REFERENCE_MATH_H
#define AUTOCALL_TWO_ASSET_THREE_DATE_MVN_REFERENCE_MATH_H

#include <array>
#include <cstdint>

namespace mvn_reference_math {

struct Rectangle {
    unsigned dimension{};
    std::array<long double, 6> lower{};
    std::array<long double, 6> upper{};
    std::array<long double, 36> covariance{};
};

struct Evaluation {
    long double primary{};
    long double independent{};
    long double paired_error{};
    long double independent_error{};
    long double mass_error{};
    unsigned primary_order{};
    unsigned independent_order{};
    std::uint64_t cdf_count{};
    bool finite{};
    bool converged{};
};

long double normal_cdf(long double x);
long double normal_pdf(long double x);
Evaluation evaluate(const Rectangle &rectangle, long double requested_error);
long double evaluate_fixed(const Rectangle &rectangle, unsigned order,
                           bool independent_order);

} // namespace mvn_reference_math

#endif
