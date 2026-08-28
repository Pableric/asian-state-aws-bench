#include "private/autocall_single_asset_three_date_greeks_diag.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

static int supported(double bump)
{
    return bump == 0.005 || bump == 0.010 || bump == 0.020;
}

static int prepare(double center,double bump,int relative,
                   autocall_3date_greek_legs_t *out)
{
    if (out == NULL || !isfinite(center) || !(center > 0.0) ||
        !isfinite(bump)) return AUTOCALL_3DATE_GREEK_INVALID;
    if (!supported(bump)) return AUTOCALL_3DATE_GREEK_UNSUPPORTED_BUMP;
    const float zero=(float)center;
    const double absolute=relative ? (double)zero*bump : bump;
    const float minus=(float)((double)zero-absolute);
    const float plus=(float)((double)zero+absolute);
    const double dm=(double)zero-(double)minus;
    const double dp=(double)plus-(double)zero;
    if (!isfinite(zero) || !isfinite(minus) || !isfinite(plus) ||
        !(minus > 0.0f) || !(dm > 0.0) || !(dp > 0.0) ||
        !isfinite(dm) || !isfinite(dp)) return AUTOCALL_3DATE_GREEK_DOMAIN;
    autocall_3date_greek_legs_t result;
    memset(&result,0,sizeof(result));
    result.minus=minus;result.zero=zero;result.plus=plus;
    result.dm=dm;result.dp=dp;
    result.first.minus=-dp/(dm*(dm+dp));
    result.first.zero=(dp-dm)/(dm*dp);
    result.first.plus=dm/(dp*(dm+dp));
    result.second.minus=2.0/(dm+dp)/dm;
    result.second.zero=-2.0/(dm+dp)*(1.0/dm+1.0/dp);
    result.second.plus=2.0/(dm+dp)/dp;
    if (!isfinite(result.first.minus) || !isfinite(result.first.zero) ||
        !isfinite(result.first.plus) || !isfinite(result.second.minus) ||
        !isfinite(result.second.zero) || !isfinite(result.second.plus))
        return AUTOCALL_3DATE_GREEK_DOMAIN;
    *out=result;
    return AUTOCALL_3DATE_GREEK_OK;
}

int autocall_3date_prepare_spot_legs(double spot,double fraction,
    autocall_3date_greek_legs_t *out)
{
    return prepare(spot,fraction,1,out);
}

int autocall_3date_prepare_volatility_legs(double sigma,double absolute_bump,
    autocall_3date_greek_legs_t *out)
{
    return prepare(sigma,absolute_bump,0,out);
}
