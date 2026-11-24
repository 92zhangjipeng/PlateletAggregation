#ifndef PUMPPOSITIONTRACKER_H
#define PUMPPOSITIONTRACKER_H

/**
 * 样本泵 / 试剂泵的当前绝对步数（0~7200）。
 *
 * 动作层发“吸/吐到某位置”命令前，用当前位置 + 吸样步数计算绝对目标；
 * 每完成一条泵命令后更新。位置未知时 valid=false，清洗等动作应拒绝执行
 * 或先排空到 0 建立已知位置。
 */
class PumpPositionTracker
{
public:
    static PumpPositionTracker &instance();

    bool sampleValid() const;
    bool reagentValid() const;
    int samplePosition() const;
    int reagentPosition() const;

    void setSamplePosition(int position);
    void setReagentPosition(int position);

    /** 开机灌注完成：样本泵 360、试剂泵 720 */
    void setBootPrimePositions();

    void invalidate();

private:
    PumpPositionTracker() = default;

    bool m_sampleValid = false;
    bool m_reagentValid = false;
    int m_samplePosition = 0;
    int m_reagentPosition = 0;
};

#endif // PUMPPOSITIONTRACKER_H
