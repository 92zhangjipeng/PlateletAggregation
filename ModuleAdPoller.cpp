#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "ModuleAdPoller.h"
#include "SerialPortManager.h"
#include "appconfig.h"

#include <algorithm>

namespace {
constexpr quint16 kAdMin = 0;   // 协议：通道 AD 有效范围
constexpr quint16 kAdMax = 4095;
constexpr int kMaxSamplesPerSecond = 256;
}

ModuleAdPoller::ModuleAdPoller(SerialPortManager *serial, QObject *parent)
    : QObject(parent)
    , m_serial(serial)
{
    m_readers[0] = new platelet::DetectionModule(
                quint8(platelet::DetectionModule::Module1));
    m_readers[1] = new platelet::DetectionModule(
                quint8(platelet::DetectionModule::Module2));
    m_readers[2] = new platelet::DetectionModule(
                quint8(platelet::DetectionModule::Module3));

    m_timer.setInterval(kPollIntervalMs);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &ModuleAdPoller::onPollTick);

    if (m_serial) {
        connect(m_serial, &SerialPortManager::frameReceived,
                this, &ModuleAdPoller::onFrameReceived);
    }

    resetState();
}

ModuleAdPoller::~ModuleAdPoller()
{
    m_timer.stop();
    for (int i = 0; i < 3; ++i) {
        delete m_readers[i];
        m_readers[i] = nullptr;
    }
}

void ModuleAdPoller::setMachineModel(MachineModel model)
{
    if (m_model == model) {
        return;
    }
    const int oldCount = activeModuleCount();
    m_model = model;
    resetState();

    emit machineModelChanged(m_model);
    const int newCount = activeModuleCount();
    if (oldCount != newCount) {
        emit moduleCountChanged(newCount);
    }
}

MachineModel ModuleAdPoller::machineModel() const
{
    return m_model;
}

int ModuleAdPoller::activeModuleCount() const
{
    return machineModuleCount(m_model);
}

void ModuleAdPoller::start()
{
    if (m_running) {
        return;
    }
    m_running = true;
    resetState();
    m_tickCounter = 0;
    m_timer.start();
    onPollTick();   // 立即采第一个样本
}

void ModuleAdPoller::stop()
{
    m_running = false;
    m_timer.stop();
}

bool ModuleAdPoller::isRunning() const
{
    return m_running;
}

void ModuleAdPoller::setBusQuiet(bool quiet)
{
    m_busQuiet = quiet;
}

bool ModuleAdPoller::isPeriodicReply(const QByteArray &frame) const
{
    if (!m_running || frame.size() < SerialPortManager::kFrameLength) {
        return false;
    }
    const quint8 address = static_cast<quint8>(frame.at(0));
    if (address < 1 || address > activeModuleCount()) {
        return false;
    }
    const ModuleState &state = m_states[address - 1];
    if (state.lastPollCmdNum == 0
            || static_cast<quint8>(frame.at(2)) != state.lastPollCmdNum) {
        return false;
    }

    QString error;
    const platelet::DetectionModule::Frame parsed =
            platelet::DetectionModule::parse(frame, &error);
    return parsed.valid && parsed.isChannelReport();
}

ModuleAdPoller::ModuleAdData ModuleAdPoller::latestModuleData(int moduleNumber) const
{
    if (moduleNumber < 1 || moduleNumber > 3) {
        ModuleAdData empty;
        empty.channels.resize(4);
        empty.channels.fill(0);
        return empty;
    }
    return m_latest[moduleNumber - 1];
}

void ModuleAdPoller::onPollTick()
{
    if (!m_running) {
        return;
    }

    // 每 10 次采样（约 1 秒）输出一次滤波值
    if (m_tickCounter > 0
            && (m_tickCounter % kPollsPerOutput) == 0) {
        emitSecondValues();
    }

    // 0x17 动作交换期间不发 0x16 读命令；滤波状态和每秒输出保持运行，
    // 保证动作结束后 AD 采集立即接上。
    if (!m_busQuiet) {
        for (int i = 0; i < activeModuleCount(); ++i) {
            pollModule(i);
        }
    }
    ++m_tickCounter;
}

void ModuleAdPoller::emitSecondValues()
{
    const int count = activeModuleCount();
    for (int moduleIndex = 0; moduleIndex < count; ++moduleIndex) {
        ModuleAdData data;
        data.moduleNumber = moduleIndex + 1;
        data.channels.resize(4);

        bool anyValid = false;
        for (int channel = 0; channel < 4; ++channel) {
            ChannelFilter &filter = m_states[moduleIndex].channels[channel];
            data.channels[channel] = filter.finalize();
            if (filter.hasValue) {
                anyValid = true;
            }
        }
        data.channelsValid = anyValid;
        data.temperatureCelsius = m_states[moduleIndex].lastTemperature;
        data.temperatureValid = m_states[moduleIndex].hasTemperature;

        m_latest[moduleIndex] = data;
        emit moduleAdUpdated(data);
    }
}

void ModuleAdPoller::onFrameReceived(const QByteArray &frame)
{
    QString error;
    const platelet::DetectionModule::Frame parsed =
            platelet::DetectionModule::parse(frame, &error);
    if (!parsed.valid || !parsed.isChannelReport()) {
        return;
    }

    const int moduleNumber = int(parsed.slaveAddress);
    if (moduleNumber < 1 || moduleNumber > activeModuleCount()) {
        return;
    }

    ModuleState &state = m_states[moduleNumber - 1];
    if (parsed.temperatureRaw != 0) {
        state.lastTemperature = parsed.temperatureCelsius;
        state.hasTemperature = true;
    }

    for (int channel = 0; channel < 4; ++channel) {
        state.channels[channel].add(parsed.channel(channel + 1));
    }
}

void ModuleAdPoller::pollModule(int moduleIndex)
{
    if (!m_running || !m_serial || !m_serial->isOpen()) {
        return;
    }
    if (moduleIndex < 0 || moduleIndex >= 3 || !m_readers[moduleIndex]) {
        return;
    }

    // 读命令的温度字段也要填当前目标温度。模组控制页保存后，
    // 这里每次轮询重新取一次设置，读回不会把目标写成旧值。
    m_readers[moduleIndex]->setTargetTemperature(
                moduleTargetTemperatureFromSettings(moduleIndex + 1));
    const QByteArray frame = m_readers[moduleIndex]->readTemperatureAndChannels();
    if (frame.size() != SerialPortManager::kFrameLength) {
        return;
    }

    m_states[moduleIndex].lastPollCmdNum = static_cast<quint8>(frame.at(2));

    QString error;
    if (!m_serial->sendFrame(frame, &error)) {
        // 断线/发送失败由主界面状态提示，这里不刷日志
    }
}

void ModuleAdPoller::resetState()
{
    for (int moduleIndex = 0; moduleIndex < 3; ++moduleIndex) {
        for (int channel = 0; channel < 4; ++channel) {
            m_states[moduleIndex].channels[channel].reset();
        }
        m_states[moduleIndex].lastTemperature = 0.0;
        m_states[moduleIndex].hasTemperature = false;
        m_states[moduleIndex].lastPollCmdNum = 0;

        m_latest[moduleIndex] = ModuleAdData();
        m_latest[moduleIndex].moduleNumber = moduleIndex + 1;
        m_latest[moduleIndex].channels.resize(4);
        m_latest[moduleIndex].channels.fill(0);
    }
}

void ModuleAdPoller::ChannelFilter::add(quint16 value)
{
    if (value < kAdMin || value > kAdMax) {
        return;   // 协议范围外的值不参与滤波
    }
    if (samples.size() >= kMaxSamplesPerSecond) {
        samples.removeFirst();
    }
    samples.append(value);
}

quint16 ModuleAdPoller::ChannelFilter::finalize()
{
    if (!samples.isEmpty()) {
        std::sort(samples.begin(), samples.end());
        const int count = samples.size();
        if (count % 2 == 1) {
            lastValue = samples.at(count / 2);
        } else {
            lastValue = static_cast<quint16>(
                        (int(samples.at(count / 2 - 1))
                         + int(samples.at(count / 2)) + 1) / 2);
        }
        hasValue = true;
        samples.clear();
    }
    return lastValue;
}

void ModuleAdPoller::ChannelFilter::reset()
{
    samples.clear();
    lastValue = 0;
    hasValue = false;
}
