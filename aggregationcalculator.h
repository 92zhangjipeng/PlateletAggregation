#ifndef AGGREGATIONCALCULATOR_H
#define AGGREGATIONCALCULATOR_H

#include <QtGlobal>
#include <cmath>

/**
 * 血小板聚集率计算（移植自老程序 displaychanneldata.cpp 的对数算法）。
 *
 * 公式：聚集率 = log10(PRPn/PRP0) / log10(PPP/PRP0)
 * 其中 PPP/PRP0 的比值 k 按分段区间选择不同的分母：
 * - k < 2.3：分母 = log10(2.3)（固定校准系数）
 * - 2.3 ≤ k < 4.0：分母 = log10(k)；若 PRPn > PPP，PRPn 收到 PPP×0.95~1.0
 * - 4.0 ≤ k < 6.5：分母 = log10(PPP×0.6/PRP0)
 * - k ≥ 6.5：分母 = log10(PPP×0.5/PRP0)
 * 结果为 0~1 的小数，乘 100 即百分比。
 */
namespace AggregationCalculator {

// 分段边界与校准系数（与老程序默认值一致，如需标定可调）
const double kK1 = 2.3;
const double kK2Min = 2.3;
const double kK2Max = 4.0;
const double kK3Min = 4.0;
const double kK3Max = 6.5;
const double kK4 = 6.5;
const double kFactor1 = 2.3;
const double kFactor3 = 0.6;
const double kFactor4 = 0.5;

/** 对数分段算法；返回 0~1 的小数，异常输入返回 0 */
inline double logarithmicRate(double prpn, double ppp, double prp0)
{
    if (prp0 == 0) {
        return 0.0;
    }
    const double k = ppp / prp0;
    double ratio = prpn / prp0;
    if (ratio <= 0) {
        return 0.0;
    }

    double denominator = 1.0;
    bool matched = false;

    if (k < kK1 && k > 0) {
        denominator = std::log10(kFactor1);
        matched = true;
    } else if (k >= kK2Min && k < kK2Max) {
        if (prpn > ppp) {
            prpn = ppp * 0.98;   // 收敛到 PPP 附近，避免比 PPP 还高
            ratio = prpn / prp0;
            if (ratio <= 0) {
                return 0.0;
            }
        }
        denominator = std::log10(k);
        matched = true;
    } else if (k >= kK3Min && k < kK3Max) {
        const double adjustedK = (ppp * kFactor3) / prp0;
        if (adjustedK <= 0) {
            return 0.0;
        }
        denominator = std::log10(adjustedK);
        matched = true;
    } else if (k <= kK4 || k >= kK4) {
        // k >= 6.5（以及兜底的所有剩余区间）
        const double adjustedK = (ppp * kFactor4) / prp0;
        if (adjustedK <= 0) {
            return 0.0;
        }
        denominator = std::log10(adjustedK);
        matched = true;
    }

    if (!matched && k <= 0) {
        return 0.0;
    }
    if (denominator == 0 || std::isnan(denominator)
            || std::isinf(denominator)) {
        return 0.0;
    }
    const double result = std::log10(ratio) / denominator;
    if (std::isnan(result) || std::isinf(result)) {
        return 0.0;
    }
    return result;
}

/** 集聚百分比（0~100），异常输入返回 0 */
inline double aggregationPercent(double prpn, double prp0, double ppp)
{
    if (prpn >= ppp) {
        prpn = ppp * 0.98;
    }
    return logarithmicRate(prpn, ppp, prp0) * 100.0;
}

} // namespace AggregationCalculator

#endif // AGGREGATIONCALCULATOR_H
