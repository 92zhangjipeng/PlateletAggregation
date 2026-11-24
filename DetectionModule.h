#ifndef DETECTIONMODULE_H
#define DETECTIONMODULE_H

#include <QByteArray>
#include <QString>
#include <QtGlobal>
#include <array>

namespace platelet {

/**
 * 检测模组协议 V1.1.0（Qt5 / QtCore，不依赖串口层）
 *
 * 一帧固定 15 字节：
 *   [0]      slave_addr    从机地址：01=模组1，10=模组2，11=模组3（二进制写法）
 *                          本类用 0x01 / 0x02 / 0x03
 *   [1]      func_code     固定 0x17
 *   [2]      cmd_num       命令编号 0x01~0xFF（本类自动轮转）
 *   [3]      cmd_size      缓存命令容量（实机现有下位机命令一直填 0x00）
 *   [4]      ctrl          cmd_func(bit7~3) | cmd_status(bit2~1) | type(bit0)
 *   [5..6]   temperature_rep   温度原始值，实际温度 = 原始值 × 0.0625（小端）
 *   [7..14]  adcvalue_rep      4 路通道值，每路 2 字节（小端）
 *
 * ctrl 字节：
 *   type      0 = 命令（上位机发送=写），1 = 报告（上位机发送=读）
 *   cmd_status 00 等待 / 01 运行中 / 10 执行完成 / 11 命令错误（从机掉线）
 *   cmd_func  00000 读取温度和通道数据
 *             00001 读取 LED 电流 / 设置 LED 光值（按 type 区分）
 *             00010 设置电机转速（可读可写）
 *             00011 保存参数（可写）
 *             00100 设置 LED 电流值（可写）
 *
 * 通道值含义随 cmd_func 变化：
 *   - 00000 读回：0~4095
 *   - 00001 type=0 写调光：调光值 0~4095
 *   - 00001 type=1 读回：LED 电流值 0~600
 *   - 00010 电机转速：700~1028，数值越大越慢，936 正常，1028 停止
 *   - 00100 写 LED 电流：0~600
 *
 * 温度字段：
 *   协议没有单独的“设温命令”。目标温度随任意写命令一起下发（协议注明：
 *   上位机发送的数值是温控目标）。本类缓存的目标温度会随每个写命令发送；
 *   调用 saveParameters() 可把当前参数（含目标温度）保存到模组。
 *
 * 16 位字段字节序：
 *   实机现有代码（GlobalData / QUIUtils）写入和读取均为“低字节在前”（小端），
 *   本类保持一致。若新固件改为大端，只需改 DetectionModule.cpp 里的
 *   appendU16LittleEndian() / readU16LittleEndian() 两处。
 *
 * 参数越界策略：
 *   调光、转速、LED 电流超出协议范围时自动钳位到合法边界，并记录到
 *   lastError()；通道号非法时返回空 QByteArray。
 *
 * 用法示例见同目录 README.md。
 */
class DetectionModule
{
public:
    enum class Func : quint8 {
        ReadTempAndChannels = 0,   // 00000
        LedLight            = 1,   // 00001
        MotorSpeed          = 2,   // 00010
        Save                = 3,   // 00011
        LedCurrent          = 4    // 00100
    };

    enum class FrameType : quint8 {
        Command = 0,               // 0：命令（写）
        Report  = 1                // 1：报告（读）
    };

    enum class Status : quint8 {
        Waiting   = 0,             // 00 等待
        Running   = 1,             // 01 运行中
        Completed = 2,             // 10 执行完成
        Error     = 3              // 11 命令错误（从机掉线）
    };

    enum Channel : int {
        Channel1 = 0,
        Channel2 = 1,
        Channel3 = 2,
        Channel4 = 3,
        ChannelCount = 4           // 每个模组 4 路
    };

    enum Slave : quint8 {
        Module1 = 0x01,            // 二进制 01
        Module2 = 0x02,            // 二进制 10
        Module3 = 0x03             // 二进制 11
    };

    enum Limits : quint16 {
        FrameSize       = 15,
        FuncCode        = 0x17,
        SpeedMin        = 700,
        SpeedMax        = 1028,
        SpeedNormal     = 936,
        SpeedStop       = 1028,
        DimmingMin      = 0,
        DimmingMax      = 4095,
        LedCurrentMin   = 0,
        LedCurrentMax   = 600
    };

    /** 温度分辨率：实际温度 = 原始值 × 0.0625 */
    static double temperatureResolution();

    /** 默认目标温度（35 ℃），可在模组控制页按模组设置 */
    static double defaultTargetTemperature();

    /** 解析后的一帧数据 */
    struct Frame {
        bool valid = false;
        quint8 slaveAddress = 0;
        quint8 funcCode = 0;
        quint8 cmdNum = 0;
        quint8 cmdSize = 0;
        quint8 ctrl = 0;
        Func func = Func::ReadTempAndChannels;
        Status status = Status::Waiting;
        FrameType type = FrameType::Report;
        quint16 temperatureRaw = 0;
        double temperatureCelsius = 0.0;
        std::array<quint16, ChannelCount> channels{{0, 0, 0, 0}};

        /** 从机掉线 / 命令错误（cmd_status == 11） */
        bool isError() const;
        /** type == 1，从机报告帧 */
        bool isReadReply() const;
        /** cmd_func=00000 且 type=1：温度 + 通道 AD 值报告 */
        bool isChannelReport() const;
        /** cmd_func=00010 且 type=1：4 路转速报告 */
        bool isSpeedReport() const;
        /** cmd_func=00001 且 type=1：4 路 LED 电流报告 */
        bool isLedCurrentReport() const;
        /** 取通道值，channel 为 1~4；非法通道返回 0 */
        quint16 channel(int channel) const;
    };

    explicit DetectionModule(quint8 slaveAddress = quint8(Slave::Module1));

    quint8 slaveAddress() const;
    bool setSlaveAddress(quint8 slaveAddress);
    static bool isValidSlaveAddress(quint8 slaveAddress);

    /** 设置目标温度（℃），只更新缓存，不会单独发帧。
     *  之后所有写命令都会携带这个目标温度。 */
    void setTargetTemperature(double celsius);
    double targetTemperature() const;
    quint16 targetTemperatureRaw() const;

    /** 最近一次构帧使用的命令编号（返回应答时用来对应请求） */
    quint8 lastCommandNumber() const;
    /** 重置命令编号（0 或 >0xFF 视为 1） */
    void resetCommandNumber(quint8 next = 1);

    /** 缓存命令容量字段，默认 0（与实机现有下位机命令一致）。
     *  协议表写 0x01~0xFF，如新固件要求可自行设置。 */
    void setCmdSize(quint8 cmdSize);
    quint8 cmdSize() const;

    // ---------------- 写命令（type = 命令 0） ----------------

    /** 设置目标温度并立即下发。
     *  协议无独立设温命令，这里用“电机转速写命令 + 4 路通道 0”实现：
     *  0 表示不改该路参数，仅携带目标温度。 */
    QByteArray setTemperature(double celsius);

    /** 4 路写同一个调光值（0~4095），越界钳位 */
    QByteArray setDimming(quint16 dimming);
    /** 只写某一路调光值（1~4），其余通道 0 = 不修改 */
    QByteArray setChannelDimming(int channel, quint16 dimming);

    /** 4 路写同一个 LED 电流值（0~600），越界钳位 */
    QByteArray setLedCurrent(quint16 current);
    /** 只写某一路 LED 电流（1~4），其余通道 0 = 不修改 */
    QByteArray setChannelLedCurrent(int channel, quint16 current);

    /** 设置某一路转速（1~4，700~1028），并记住该值供 startChannel() 使用。
     *  1028 表示停止，936 为正常转速。其余通道 0 = 不修改。 */
    QByteArray setSpeed(int channel, quint16 speed);
    /** 4 路设置同一转速，并记住该值 */
    QByteArray setAllSpeeds(quint16 speed);

    /** 让某一路按之前 setSpeed/startChannel 记下的转速旋转 */
    QByteArray startChannel(int channel);
    /** 4 路都按各自记下的转速旋转 */
    QByteArray startAllChannels();
    /** 停止某一路（转速 1028），不覆盖已记下的转速 */
    QByteArray stopChannel(int channel);
    /** 停止全部 4 路（转速 1028），不覆盖已记下的转速 */
    QByteArray stopAllChannels();

    /** 保存模组参数（含当前目标温度） */
    QByteArray saveParameters();

    // ---------------- 读命令（type = 报告 1） ----------------

    /** 读取温度和 4 路通道数据（cmd_func=00000） */
    QByteArray readTemperatureAndChannels();
    /** 读取 4 路 LED 电流（cmd_func=00001，type=1） */
    QByteArray readLedCurrent();
    /** 读取 4 路电机转速（cmd_func=00010，type=1） */
    QByteArray readSpeed();

    /** 已记录的通道转速（未设置过则为 936；非法通道返回 0） */
    quint16 configuredSpeed(int channel) const;

    /** 最近一次调用的错误/钳位提示，成功时为空 */
    QString lastError() const;

    /** 解析 15 字节应答帧；frame 至少 15 字节，取前 15 字节。
     *  解析失败时 valid=false，并写入 errorString。 */
    static Frame parse(const QByteArray &frame, QString *errorString = nullptr);

    static bool isValidDimming(quint16 value);
    static bool isValidSpeed(quint16 value);
    static bool isValidLedCurrent(quint16 value);

    static QString funcText(Func func);
    static QString statusText(Status status);

private:
    QByteArray build(Func func, FrameType type,
                     const std::array<quint16, ChannelCount> &channels,
                     quint16 temperatureRaw);
    bool checkChannel(int channel);
    quint16 clampDimming(quint16 value);
    quint16 clampSpeed(quint16 value);
    quint16 clampLedCurrent(quint16 value);
    quint8 takeCommandNumber();

    quint8 m_slaveAddress = quint8(Slave::Module1);
    quint8 m_nextCmdNum = 1;
    quint8 m_lastCmdNum = 0;
    quint8 m_cmdSize = 0;
    quint16 m_targetTempRaw = 0;
    std::array<quint16, ChannelCount> m_speeds{{SpeedNormal, SpeedNormal,
                                                SpeedNormal, SpeedNormal}};
    QString m_lastError;
};

} // namespace platelet

#endif // DETECTIONMODULE_H
