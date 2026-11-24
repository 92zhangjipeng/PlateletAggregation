#ifndef MOTIONPROTOCOL_H
#define MOTIONPROTOCOL_H

#include <QByteArray>
#include <QMetaType>
#include <QString>
#include <QtGlobal>

namespace platelet {

/**
 * 运动控制协议（功能码 0x17，固定 15 字节帧）。
 *
 * 帧结构：
 *   [0]      slave_addr  0x04=Z 轴板，0x05=主控板
 *   [1]      func_code   固定 0x17
 *   [2]      cmd_num     1~255，本类自动轮转
 *   [3]      cmd_size    命令缓存容量（旧上位机固定填 0，本类默认 0）
 *   [4]      ctrl        axis(bit7~5) | operation_mode(bit4~3)
 *                        | cmd_status(bit2~1) | type(bit0)
 *   [5]      axis_byte   主板：sport_asix(bit1~0) | area(bit3~2)
 *                        | wait(bit4) | switch(bit5) | pump(bit6) | send(bit7)
 *                        Z 轴板：sport_asix(bit1~0) | area(bit3~2)
 *                        | gripper_status(bit4) | mode(bit5)
 *                        | status_stop_l(bit6) | status_stop_r(bit7)
 *   [6..9]   distance1   小端有符号 32 位
 *   [10..13] distance2   小端有符号 32 位
 *   [14]     fault       命令帧填 0，报告帧为故障码（1~5）
 *
 * 电机轴（axis 字段，十进制）：
 *   0 = Z1 抓手（X 电机）
 *   1 = Z2 试剂针（Y 电机）
 *   2 = Z3 样本针；命令发到主控板时同一轴号表示 2500ul 血样泵
 *   3 = 100ul 试剂泵
 *   4 = XY 联动
 *
 * 地址分配：
 *   Z1/Z2/Z3 下针、复位        → 0x04（Z 轴板）
 *   XY、血样泵、试剂泵         → 0x05（主控板）
 *
 * 单位：
 *   XY 与 Z 使用与坐标表一致的整数（mm）；柱塞泵行程为 0~7200。
 *   4 字节距离为小端有符号整数，与旧上位机 intToByte() 一致。
 *
 * 本类只负责构帧/解帧，不依赖串口；发送请配合 MotionController。
 */
class MotionProtocol
{
public:
    enum Slave : quint8 {
        ZAxisBoard = 0x04,
        MainBoard = 0x05
    };

    enum class Axis : quint8 {
        Gripper = 0,        // Z1：抓手
        ReagentNeedle = 1,  // Z2：试剂针
        SampleNeedle = 2,   // Z3：样本针
        BloodPump = 2,      // 2500ul 血样泵（主控板，与样本针共用轴号）
        ReagentPump = 3,    // 100ul 试剂泵
        Xy = 4              // XY 联动
    };

    enum class OperationMode : quint8 {
        Position = 0,       // 位置模式
        PositiveSpeed = 1,  // 正速度（液面探测）
        NegativeSpeed = 2,  // 负速度（电机复位）
        Hold = 3            // 保持
    };

    enum class FrameType : quint8 {
        Command = 0,
        Report = 1
    };

    enum class Status : quint8 {
        Waiting = 0,
        Running = 1,
        Completed = 2,
        Error = 3
    };

    /** XY 运动坐标相对于哪根针/工具（协议 bit1~0） */
    enum class XyReference : quint8 {
        BloodNeedle = 0,
        ReagentNeedle = 1,
        Gripper = 2
    };

    /** 坐标分区（协议 bit3~2） */
    enum class Area : quint8 {
        Test = 0,
        EmptyCup = 1,
        BloodSample = 2,
        DiscardCup = 3
    };

    enum Limits : int {
        FrameSize = 15,
        FuncCode = 0x17,
        PumpFullScale = 7200,   // 柱塞泵满行程：样本泵 2500 µL / 试剂泵 100 µL
        DefaultXyResetSpeed = 720,
        DefaultZResetSpeed = 100,
        DefaultPumpResetSpeed = 2160
    };

    struct Frame
    {
        bool valid = false;
        quint8 slaveAddress = 0;
        quint8 funcCode = 0;
        quint8 cmdNum = 0;
        quint8 cmdSize = 0;
        quint8 ctrl = 0;
        FrameType type = FrameType::Report;
        Status status = Status::Waiting;
        OperationMode mode = OperationMode::Position;
        Axis axis = Axis::Gripper;
        quint8 axisByte = 0;
        qint32 distance1 = 0;
        qint32 distance2 = 0;
        quint8 fault = 0;

        bool isReport() const;
        bool isError() const;
        QString faultDescription() const;

        /** 抓手报告帧 distance2 中的负压原始值；非抓手报告返回 0 */
        quint32 gripperVacuum() const;
        /** 抓手报告帧 axisByte bit4：1=吸/抓杯，0=吐/松杯 */
        bool gripperSuctionBit() const;
    };

    MotionProtocol();

    quint8 lastCommandNumber() const;
    /** 重置命令编号；0 视为 1 */
    void resetCommandNumber(quint8 next = 0);

    /** 命令缓存容量字段，默认 0（与旧上位机一致） */
    void setCmdSize(quint8 cmdSize);
    quint8 cmdSize() const;

    /** 最近一次构帧失败的原因；成功时为空 */
    QString lastError() const;

    // ---------------- 单动作命令 ----------------

    /** XY 联动移动到 (x, y)，reference 指定坐标相对哪根工具 */
    QByteArray moveXy(int x, int y,
                      XyReference reference = XyReference::BloodNeedle,
                      Area area = Area::Test);

    /**
     * 抓手下降到 height（位置模式）。
     * grabCup 对应 Z 轴字节 gripper_status（bit4），也就是抓手空气阀开关：
     * true=开阀抓杯，false=关阀弃杯/松杯。只控制抓手时有效。
     */
    QByteArray moveGripperTo(int height, Area area = Area::Test,
                             bool grabCup = false,
                             bool liquidLevel = false);
    /**
     * 抓手正速度（速度模式）下压吸杯：以 speed 匀速下降，
     * limitHeight 为行程上限（用试管位 z 值），到位后返回完成报告，
     * 报告帧 distance2 携带负压值。
     */
    QByteArray moveGripperDownSpeed(int speed, int limitHeight,
                                    Area area = Area::Test,
                                    bool grabCup = true);
    /** 试剂针下降到 height；liquidLevel=true 时 height 为探测失败后的下探高度 */
    QByteArray moveReagentNeedleTo(int height, Area area = Area::Test,
                                   bool liquidLevel = false);
    /** 样本针下降到 height；liquidLevel=true 时 height 为探测失败后的下探高度 */
    QByteArray moveSampleNeedleTo(int height, Area area = Area::Test,
                                  bool liquidLevel = false);

    /** XY 负速度复位，speed 默认 720 */
    QByteArray resetXy(int speed = DefaultXyResetSpeed);
    /**
     * 三个 Z 轴负速度复位，speed 默认 100。
     * 抓手复位可带 grabCup=true，保持空气阀开启把杯子带到顶部再横移。
     */
    QByteArray resetGripper(int speed = DefaultZResetSpeed,
                            Area area = Area::Test,
                            bool grabCup = false);
    QByteArray resetReagentNeedle(int speed = DefaultZResetSpeed,
                                  Area area = Area::Test);
    QByteArray resetSampleNeedle(int speed = DefaultZResetSpeed,
                                 Area area = Area::Test);

    /**
     * 血样泵（2500ul）/试剂泵（100ul）位置模式移动。
     * units 为 0~7200 的行程单位；aspirate=true 表示吸样等待标志；
     * clean=true 会同时打开隔膜泵和开关阀，cleaningTimeMs 写入 distance2
     * （协议规定为清洗时间，常用 6000ms）。
     */
    QByteArray moveBloodPump(int units, bool aspirate, bool clean = false,
                             int cleaningTimeMs = 0);
    QByteArray moveReagentPump(int units, bool aspirate, bool clean = false,
                               int cleaningTimeMs = 0);

    /** 柱塞泵负速度复位，speed 默认 2160 */
    QByteArray resetBloodPump(int speed = DefaultPumpResetSpeed,
                              bool clean = false);
    QByteArray resetReagentPump(int speed = DefaultPumpResetSpeed,
                                bool clean = false);

    // ---------------- 解帧 / 文本 ----------------
    static Frame parse(const QByteArray &frame,
                       QString *errorString = nullptr);

    static QString axisName(Axis axis);
    static QString areaName(Area area);
    static QString xyReferenceName(XyReference reference);
    static QString operationModeName(OperationMode mode);
    static QString statusText(Status status);
    static QString faultText(quint8 fault);

private:
    QByteArray buildCommand(Slave slave, Axis axis, OperationMode mode,
                            quint8 axisByte,
                            qint32 distance1, qint32 distance2);
    QByteArray buildZMove(Axis axis, qint32 height, Area area,
                          bool grabCup, bool liquidLevel);
    QByteArray buildZSpeedMove(Axis axis, int speed, int limitHeight,
                               Area area, bool grabCup);
    QByteArray buildZReset(Axis axis, int speed, Area area,
                           bool grabCup = false);
    QByteArray buildPumpAction(Axis axis, OperationMode mode, int units,
                               bool aspirate, bool clean,
                               int cleaningTimeMs);
    bool checkPumpDistance(int units);
    quint8 takeCommandNumber();
    static quint8 zReference(Axis axis);

    quint8 m_nextCmdNum = 0;
    quint8 m_lastCmdNum = 0;
    quint8 m_cmdSize = 0;
    QString m_lastError;
};

} // namespace platelet

Q_DECLARE_METATYPE(platelet::MotionProtocol::Frame)

#endif // MOTIONPROTOCOL_H
