#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "modulecontrolwidget.h"
#include "CommunicationWorker.h"
#include "appconfig.h"

#include <QMetaObject>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStyle>
#include <QTabWidget>
#include <QTime>
#include <QVBoxLayout>

using platelet::DetectionModule;

namespace {

QString frameHex(const QByteArray &frame)
{
    return QString::fromLatin1(frame.toHex(' ').toUpper().constData());
}

QPushButton *makeButton(const QString &text, const char *styleName, QWidget *parent)
{
    auto *button = new QPushButton(text, parent);
    button->setObjectName(QString::fromLatin1(styleName));
    button->setCursor(Qt::PointingHandCursor);
    button->setMinimumHeight(30);
    return button;
}

} // namespace

ModulePanel::ModulePanel(int moduleNumber, QWidget *parent)
    : QWidget(parent)
    , m_module(static_cast<quint8>(moduleNumber))
    , m_moduleNumber(moduleNumber)
{
    buildUi();
}

quint8 ModulePanel::slaveAddress() const
{
    return m_module.slaveAddress();
}

int ModulePanel::globalChannelNumber(int localChannel1Based) const
{
    // 模组1=通道1~4，模组2=通道5~8，模组3=通道9~12
    return (m_moduleNumber - 1) * 4 + localChannel1Based;
}

void ModulePanel::buildUi()
{
    auto *rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(14);

    auto *leftLayout = new QVBoxLayout;
    leftLayout->setSpacing(10);
    leftLayout->addWidget(createTemperatureGroup());
    leftLayout->addWidget(createDimmingGroup());
    leftLayout->addWidget(createCurrentGroup());
    leftLayout->addStretch();

    auto *rightLayout = new QVBoxLayout;
    rightLayout->setSpacing(10);
    rightLayout->addWidget(createSpeedGroup());
    rightLayout->addWidget(createReadbackGroup());
    rightLayout->addWidget(createLogGroup(), 1);

    rootLayout->addLayout(leftLayout, 1);
    rootLayout->addLayout(rightLayout, 1);
}

QGroupBox *ModulePanel::createTemperatureGroup()
{
    auto *group = new QGroupBox(tr("温度控制"), this);
    auto *grid = new QGridLayout(group);
    grid->setContentsMargins(14, 18, 14, 14);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(10);

    auto *caption = new QLabel(tr("目标温度"), group);

    m_targetTempSpin = new MedicalDoubleSpinBox(group);
    m_targetTempSpin->setRange(0.0, 100.0);
    m_targetTempSpin->setDecimals(2);
    m_targetTempSpin->setSingleStep(0.5);
    m_targetTempSpin->setSuffix(QStringLiteral(" ℃"));
    // 默认 35℃，可在本页按模组设置；开机升温使用该值判断是否到位。
    m_targetTempSpin->setValue(
                moduleTargetTemperatureFromSettings(m_moduleNumber));
    m_module.setTargetTemperature(m_targetTempSpin->value());

    auto *applyButton = makeButton(tr("应用温度"), "primaryButton", group);
    auto *saveButton = makeButton(tr("保存参数"), "secondaryButton", group);

    m_currentTempLabel = new QLabel(tr("当前温度: -- ℃"), group);
    m_currentTempLabel->setObjectName(QStringLiteral("readbackValue"));

    grid->addWidget(caption, 0, 0);
    grid->addWidget(m_targetTempSpin, 0, 1);
    grid->addWidget(applyButton, 0, 2);
    grid->addWidget(saveButton, 0, 3);
    grid->addWidget(m_currentTempLabel, 1, 0, 1, 4);
    grid->setColumnStretch(1, 1);

    connect(applyButton, &QPushButton::clicked, this, [this]() {
        const double target = m_targetTempSpin->value();
        setModuleTargetTemperatureInSettings(m_moduleNumber, target);
        sendCommand(m_module.setTemperature(target),
                    tr("应用目标温度 %1 ℃")
                    .arg(target, 0, 'f', 2));
    });
    connect(saveButton, &QPushButton::clicked, this, [this]() {
        sendCommand(m_module.saveParameters(), tr("保存模组参数"));
    });

    return group;
}

QGroupBox *ModulePanel::createDimmingGroup()
{
    auto *group = new QGroupBox(tr("LED 调光"), this);
    auto *grid = new QGridLayout(group);
    grid->setContentsMargins(14, 18, 14, 14);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(10);

    auto *caption = new QLabel(tr("4 路统一调光 (0~4095)"), group);

    m_dimmingSpin = new MedicalSpinBox(group);
    m_dimmingSpin->setRange(DetectionModule::DimmingMin, DetectionModule::DimmingMax);
    m_dimmingSpin->setValue(2400);

    auto *writeButton = makeButton(tr("写入"), "primaryButton", group);

    grid->addWidget(caption, 0, 0);
    grid->addWidget(m_dimmingSpin, 0, 1);
    grid->addWidget(writeButton, 0, 2);
    grid->setColumnStretch(1, 1);

    connect(writeButton, &QPushButton::clicked, this, [this]() {
        sendCommand(m_module.setDimming(static_cast<quint16>(m_dimmingSpin->value())),
                    tr("4 路调光 %1").arg(m_dimmingSpin->value()));
    });

    return group;
}

QGroupBox *ModulePanel::createCurrentGroup()
{
    auto *group = new QGroupBox(tr("LED 电流"), this);
    auto *grid = new QGridLayout(group);
    grid->setContentsMargins(14, 18, 14, 14);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(8);

    for (int i = 0; i < 4; ++i) {
        const int globalChannel = globalChannelNumber(i + 1);
        auto *caption = new QLabel(tr("CH%1").arg(globalChannel), group);

        m_currentSpins[i] = new MedicalSpinBox(group);
        m_currentSpins[i]->setRange(DetectionModule::LedCurrentMin,
                                    DetectionModule::LedCurrentMax);
        m_currentSpins[i]->setValue(300);

        auto *writeButton = makeButton(tr("写入"), "secondaryButton", group);
        grid->addWidget(caption, i, 0);
        grid->addWidget(m_currentSpins[i], i, 1);
        grid->addWidget(writeButton, i, 2);

        connect(writeButton, &QPushButton::clicked, this, [this, i, globalChannel]() {
            const int value = m_currentSpins[i]->value();
            sendCommand(m_module.setChannelLedCurrent(i + 1, static_cast<quint16>(value)),
                        tr("CH%1 LED 电流 %2").arg(globalChannel).arg(value));
        });
    }
    grid->setColumnStretch(1, 1);

    return group;
}

QGroupBox *ModulePanel::createSpeedGroup()
{
    auto *group = new QGroupBox(tr("电机转速"), this);
    auto *grid = new QGridLayout(group);
    grid->setContentsMargins(14, 18, 14, 14);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(8);

    for (int i = 0; i < 4; ++i) {
        const int globalChannel = globalChannelNumber(i + 1);
        auto *caption = new QLabel(tr("CH%1").arg(globalChannel), group);

        m_speedSpins[i] = new MedicalSpinBox(group);
        m_speedSpins[i]->setRange(DetectionModule::SpeedMin, DetectionModule::SpeedMax);
        m_speedSpins[i]->setValue(DetectionModule::SpeedNormal);
        m_speedSpins[i]->setToolTip(tr("700~1028，数值越大越慢；936 正常，1028 停止"));

        auto *startButton = makeButton(tr("启动"), "primaryButton", group);
        auto *stopButton = makeButton(tr("停止"), "stopButton", group);

        grid->addWidget(caption, i, 0);
        grid->addWidget(m_speedSpins[i], i, 1);
        grid->addWidget(startButton, i, 2);
        grid->addWidget(stopButton, i, 3);

        connect(startButton, &QPushButton::clicked, this, [this, i, globalChannel]() {
            const int value = m_speedSpins[i]->value();
            sendCommand(m_module.setSpeed(i + 1, static_cast<quint16>(value)),
                        tr("CH%1 转速 %2").arg(globalChannel).arg(value));
        });
        connect(stopButton, &QPushButton::clicked, this, [this, i, globalChannel]() {
            sendCommand(m_module.stopChannel(i + 1), tr("CH%1 停止").arg(globalChannel));
        });
    }

    auto *startAllButton = makeButton(tr("全部启动"), "primaryButton", group);
    auto *stopAllButton = makeButton(tr("全部停止"), "stopButton", group);
    grid->addWidget(startAllButton, 4, 0, 1, 2);
    grid->addWidget(stopAllButton, 4, 2, 1, 2);
    grid->setColumnStretch(1, 1);

    connect(startAllButton, &QPushButton::clicked, this, [this]() {
        for (int i = 0; i < 4; ++i) {
            const int value = m_speedSpins[i]->value();
            sendCommand(m_module.setSpeed(i + 1, static_cast<quint16>(value)),
                        tr("CH%1 转速 %2").arg(globalChannelNumber(i + 1)).arg(value));
        }
    });
    connect(stopAllButton, &QPushButton::clicked, this, [this]() {
        sendCommand(m_module.stopAllChannels(), tr("全部停止"));
    });

    return group;
}

QGroupBox *ModulePanel::createReadbackGroup()
{
    auto *group = new QGroupBox(tr("读回"), this);
    auto *layout = new QVBoxLayout(group);
    layout->setContentsMargins(14, 18, 14, 14);
    layout->setSpacing(8);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->setSpacing(8);
    auto *readChannelButton = makeButton(tr("读温度/通道"), "secondaryButton", group);
    auto *readSpeedButton = makeButton(tr("读转速"), "secondaryButton", group);
    auto *readCurrentButton = makeButton(tr("读电流"), "secondaryButton", group);
    buttonRow->addWidget(readChannelButton);
    buttonRow->addWidget(readSpeedButton);
    buttonRow->addWidget(readCurrentButton);
    buttonRow->addStretch();
    layout->addLayout(buttonRow);

    auto *channelRow = new QHBoxLayout;
    channelRow->setSpacing(14);
    channelRow->addWidget(new QLabel(tr("通道 AD:"), group));
    for (int i = 0; i < 4; ++i) {
        m_channelValueLabels[i] = new QLabel(
                    tr("CH%1 --").arg(globalChannelNumber(i + 1)), group);
        m_channelValueLabels[i]->setObjectName(QStringLiteral("readbackValue"));
        channelRow->addWidget(m_channelValueLabels[i]);
    }
    channelRow->addStretch();
    layout->addLayout(channelRow);

    auto *speedRow = new QHBoxLayout;
    speedRow->setSpacing(14);
    speedRow->addWidget(new QLabel(tr("转速回读:"), group));
    for (int i = 0; i < 4; ++i) {
        m_speedReadLabels[i] = new QLabel(
                    tr("CH%1 --").arg(globalChannelNumber(i + 1)), group);
        m_speedReadLabels[i]->setObjectName(QStringLiteral("readbackValue"));
        speedRow->addWidget(m_speedReadLabels[i]);
    }
    speedRow->addStretch();
    layout->addLayout(speedRow);

    auto *currentRow = new QHBoxLayout;
    currentRow->setSpacing(14);
    currentRow->addWidget(new QLabel(tr("电流回读:"), group));
    for (int i = 0; i < 4; ++i) {
        m_currentReadLabels[i] = new QLabel(
                    tr("CH%1 --").arg(globalChannelNumber(i + 1)), group);
        m_currentReadLabels[i]->setObjectName(QStringLiteral("readbackValue"));
        currentRow->addWidget(m_currentReadLabels[i]);
    }
    currentRow->addStretch();
    layout->addLayout(currentRow);

    connect(readChannelButton, &QPushButton::clicked, this, [this]() {
        sendCommand(m_module.readTemperatureAndChannels(), tr("读温度/通道数据"));
    });
    connect(readSpeedButton, &QPushButton::clicked, this, [this]() {
        sendCommand(m_module.readSpeed(), tr("读 4 路转速"));
    });
    connect(readCurrentButton, &QPushButton::clicked, this, [this]() {
        sendCommand(m_module.readLedCurrent(), tr("读 4 路 LED 电流"));
    });

    return group;
}

QGroupBox *ModulePanel::createLogGroup()
{
    auto *group = new QGroupBox(tr("通信日志"), this);
    auto *layout = new QVBoxLayout(group);
    layout->setContentsMargins(14, 18, 14, 14);

    m_log = new QPlainTextEdit(group);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(1000);
    m_log->setMinimumHeight(110);
    layout->addWidget(m_log);

    return group;
}

void ModulePanel::sendCommand(const QByteArray &frame, const QString &description)
{
    const QString time = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    if (frame.isEmpty()) {
        appendLog(tr("[%1] 模组%2 %3：生成失败（%4）")
                  .arg(time)
                  .arg(m_moduleNumber)
                  .arg(description)
                  .arg(m_module.lastError()));
        return;
    }

    appendLog(tr("[%1] 模组%2 %3 → %4")
              .arg(time)
              .arg(m_moduleNumber)
              .arg(description)
              .arg(frameHex(frame)));
    emit frameReady(frame, description);
}

void ModulePanel::appendLog(const QString &text)
{
    if (m_log) {
        m_log->appendPlainText(text);
    }
}

void ModulePanel::appendSystemMessage(const QString &text)
{
    appendLog(tr("[系统] %1").arg(text));
}

void ModulePanel::handleIncomingFrame(const QByteArray &frame)
{
    appendLog(tr("[%1] 模组%2 ← %3")
              .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")))
              .arg(m_moduleNumber)
              .arg(frameHex(frame)));

    QString error;
    const DetectionModule::Frame parsed = DetectionModule::parse(frame, &error);
    if (!parsed.valid) {
        appendLog(tr("    解析失败: %1").arg(error));
        return;
    }
    if (parsed.slaveAddress != m_module.slaveAddress()) {
        return;
    }

    appendLog(tr("    状态: %1    功能: %2")
              .arg(DetectionModule::statusText(parsed.status))
              .arg(DetectionModule::funcText(parsed.func)));
    if (parsed.isError()) {
        appendLog(tr("    命令错误 / 从机掉线"));
        return;
    }

    if (parsed.isChannelReport()) {
        // 通道 AD 的显示由每秒滤波结果统一刷新，这里只更新温度
        m_currentTempLabel->setText(
                    tr("当前温度: %1 ℃").arg(parsed.temperatureCelsius, 0, 'f', 2));
        appendLog(tr("    温度 %1 ℃，通道 %2 / %3 / %4 / %5")
                  .arg(parsed.temperatureCelsius, 0, 'f', 2)
                  .arg(parsed.channel(1))
                  .arg(parsed.channel(2))
                  .arg(parsed.channel(3))
                  .arg(parsed.channel(4)));
    }

    if (parsed.isSpeedReport()) {
        for (int i = 0; i < 4; ++i) {
            const quint16 value = parsed.channel(i + 1);
            m_speedReadLabels[i]->setText(
                        tr("CH%1: %2").arg(globalChannelNumber(i + 1)).arg(value));
            if (DetectionModule::isValidSpeed(value)) {
                m_speedSpins[i]->setValue(value);
            }
        }
    }

    if (parsed.isLedCurrentReport()) {
        for (int i = 0; i < 4; ++i) {
            const quint16 value = parsed.channel(i + 1);
            m_currentReadLabels[i]->setText(
                        tr("CH%1: %2").arg(globalChannelNumber(i + 1)).arg(value));
            if (DetectionModule::isValidLedCurrent(value)) {
                m_currentSpins[i]->setValue(value);
            }
        }
    }
}

void ModulePanel::applyAdData(const ModuleAdPoller::ModuleAdData &data)
{
    if (data.moduleNumber != m_moduleNumber) {
        return;
    }

    const bool valid = data.channelsValid && data.channels.size() >= 4;
    for (int i = 0; i < 4; ++i) {
        const int channelNumber = globalChannelNumber(i + 1);
        if (valid) {
            m_channelValueLabels[i]->setText(
                        tr("CH%1: %2").arg(channelNumber).arg(data.channels.at(i)));
        } else {
            m_channelValueLabels[i]->setText(tr("CH%1: --").arg(channelNumber));
        }
    }

    if (data.temperatureValid) {
        m_currentTempLabel->setText(
                    tr("当前温度: %1 ℃").arg(data.temperatureCelsius, 0, 'f', 2));
    }
}

ModuleControlWidget::ModuleControlWidget(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("moduleControlRoot"));

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(8);

    m_hint = new QLabel(tr("串口未连接：命令帧只生成并显示在日志中，主界面正在自动查找/重连仪器串口。"),
                        this);
    m_hint->setObjectName(QStringLiteral("commHint"));
    m_hint->setWordWrap(true);
    rootLayout->addWidget(m_hint);

    m_tabs = new QTabWidget(this);
    for (int i = 0; i < 3; ++i) {
        m_panels[i] = new ModulePanel(i + 1, m_tabs);
        m_tabs->addTab(m_panels[i], tr("模组 %1").arg(i + 1));
        connect(m_panels[i], &ModulePanel::frameReady,
                this, &ModuleControlWidget::frameReady);
    }
    rootLayout->addWidget(m_tabs, 1);

    setStyleSheet(QStringLiteral(
        "QWidget#moduleControlRoot { background: transparent; }"
        "QLabel#commHint {"
        "  color: #8A6D3B;"
        "  background: #FFF8E6;"
        "  border: 1px solid #F0DFB6;"
        "  border-radius: 6px;"
        "  padding: 8px 10px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QLabel#commHint[connected=\"true\"] {"
        "  color: #1E6B45;"
        "  background: #EDF8F1;"
        "  border: 1px solid #B8E0C8;"
        "}"
        "QTabWidget::pane {"
        "  border: 1px solid #DCE7F1;"
        "  border-radius: 8px;"
        "  background: #F7FAFD;"
        "}"
        "QTabBar::tab {"
        "  background: #E3EEF8;"
        "  color: #1F4E79;"
        "  padding: 8px 24px;"
        "  border-top-left-radius: 6px;"
        "  border-top-right-radius: 6px;"
        "  margin-right: 4px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 13px;"
        "}"
        "QTabBar::tab:selected {"
        "  background: #FFFFFF;"
        "  font-weight: 600;"
        "}"
        "QGroupBox {"
        "  background: #FFFFFF;"
        "  border: 1px solid #DCE7F1;"
        "  border-radius: 8px;"
        "  margin-top: 14px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 13px;"
        "  font-weight: 600;"
        "  color: #1F4E79;"
        "}"
        "QGroupBox::title {"
        "  subcontrol-origin: margin;"
        "  left: 14px;"
        "  padding: 0px 6px;"
        "}"
        "QLabel {"
        "  color: #3A5065;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QLabel#readbackValue {"
        "  color: #1F4E79;"
        "  font-weight: 600;"
        "}"
        "QPushButton#primaryButton {"
        "  background: #1F4E79;"
        "  color: #FFFFFF;"
        "  border: none;"
        "  border-radius: 5px;"
        "  padding: 0px 14px;"
        "  min-height: 30px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QPushButton#primaryButton:hover { background: #2C6398; }"
        "QPushButton#primaryButton:pressed { background: #163B5C; }"
        "QPushButton#secondaryButton {"
        "  background: #FFFFFF;"
        "  color: #1F4E79;"
        "  border: 1px solid #8FB4D6;"
        "  border-radius: 5px;"
        "  padding: 0px 14px;"
        "  min-height: 30px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QPushButton#secondaryButton:hover { background: #E8F1FA; }"
        "QPushButton#secondaryButton:pressed { background: #D3E6F5; }"
        "QPushButton#stopButton {"
        "  background: #FFFFFF;"
        "  color: #C0392B;"
        "  border: 1px solid #E3A8A0;"
        "  border-radius: 5px;"
        "  padding: 0px 14px;"
        "  min-height: 30px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QPushButton#stopButton:hover { background: #FDEDEB; }"
        "QPushButton#stopButton:pressed { background: #F8DBD7; }"
        "QPlainTextEdit {"
        "  background: #0F1E2B;"
        "  color: #B8E0FF;"
        "  border: 1px solid #1F4E79;"
        "  border-radius: 6px;"
        "  font-family: 'Consolas';"
        "  font-size: 11px;"
        "  padding: 4px;"
        "}"));
}

void ModuleControlWidget::handleIncomingFrame(const QByteArray &frame)
{
    if (frame.isEmpty()) {
        return;
    }
    const quint8 address = static_cast<quint8>(frame.at(0));
    for (int i = 0; i < 3; ++i) {
        if (m_panels[i] && m_panels[i]->slaveAddress() == address) {
            m_panels[i]->handleIncomingFrame(frame);
            return;
        }
    }
}

void ModuleControlWidget::setSerialConnected(bool connected, const QString &detail)
{
    if (!m_hint) {
        return;
    }

    if (connected) {
        m_hint->setText(detail.isEmpty()
                        ? tr("串口已连接，命令帧会直接发送到仪器。")
                        : tr("串口已连接（%1），命令帧会直接发送到仪器。").arg(detail));
    } else {
        m_hint->setText(tr("串口未连接：命令帧只生成并显示在日志中，主界面正在自动查找/重连仪器串口。"));
    }

    m_hint->setProperty("connected", connected);
    m_hint->style()->unpolish(m_hint);
    m_hint->style()->polish(m_hint);
    m_hint->update();
}

void ModuleControlWidget::appendSystemMessage(const QString &text)
{
    if (!m_tabs) {
        return;
    }
    if (auto *panel = qobject_cast<ModulePanel *>(m_tabs->currentWidget())) {
        panel->appendSystemMessage(text);
    }
}

void ModuleControlWidget::bindCommunicationWorker(CommunicationWorker *worker)
{
    m_commWorker = worker;
    if (!worker) {
        return;
    }

    connect(worker, &CommunicationWorker::moduleAdUpdated,
            this, &ModuleControlWidget::onModuleAdUpdated);
    connect(worker, &CommunicationWorker::moduleCountChanged,
            this, &ModuleControlWidget::setActiveModuleCount);

    // 打开对话框时请求当前活动模组数和最近一次滤波结果
    QMetaObject::invokeMethod(worker, "requestLatestModuleData",
                              Qt::QueuedConnection);
}

void ModuleControlWidget::onModuleAdUpdated(const ModuleAdPoller::ModuleAdData &data)
{
    const int index = data.moduleNumber - 1;
    if (index >= 0 && index < 3 && m_panels[index]) {
        m_panels[index]->applyAdData(data);
    }
}

void ModuleControlWidget::setActiveModuleCount(int count)
{
    if (!m_tabs) {
        return;
    }
    count = qBound(1, count, 3);

    for (int i = 0; i < 3; ++i) {
        const bool active = i < count;
        m_tabs->setTabEnabled(i, active);
        m_tabs->setTabToolTip(i, active
                              ? QString()
                              : tr("当前机型只有 %1 个模组，此页不可用").arg(count));
    }

    if (m_tabs->currentIndex() >= count) {
        m_tabs->setCurrentIndex(0);
    }
}
