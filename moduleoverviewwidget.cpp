#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "moduleoverviewwidget.h"
#include "modulerackwidget.h"
#include "testcurvedialog.h"
#include "testsession.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSizePolicy>
#include <QStyle>
#include <QVBoxLayout>

namespace {
constexpr int kMaxModules = 3;
constexpr int kModuleSpacing = 16;
constexpr int kMaxModuleWidth = 540;
constexpr int kMinModuleWidth = 250;
constexpr int kMinModuleHeight = 300;
constexpr int kMaxModuleHeight = 520;
}

ModuleOverviewWidget::ModuleOverviewWidget(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("moduleOverview"));
    setMinimumHeight(380);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(12);

    auto *headerLayout = new QHBoxLayout;
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(16);

    auto *titleBox = new QVBoxLayout;
    titleBox->setContentsMargins(0, 0, 0, 0);
    titleBox->setSpacing(2);

    m_titleLabel = new QLabel(tr("检测模组"), this);
    m_titleLabel->setObjectName(QStringLiteral("moduleOverviewTitle"));
    m_subtitleLabel = new QLabel(this);
    m_subtitleLabel->setObjectName(QStringLiteral("moduleOverviewSubtitle"));

    titleBox->addWidget(m_titleLabel);
    titleBox->addWidget(m_subtitleLabel);
    headerLayout->addLayout(titleBox);
    headerLayout->addStretch(1);

    m_testStateLabel = new QLabel(this);
    m_testStateLabel->setObjectName(QStringLiteral("globalTestStateBadge"));
    headerLayout->addWidget(m_testStateLabel, 0, Qt::AlignVCenter);

    m_legendLabel = new QLabel(
                tr("<span style='color:#2F93E0'>●</span> 测试中&nbsp;&nbsp;"
                   "<span style='color:#22A877'>●</span> 就绪&nbsp;&nbsp;"
                   "<span style='color:#859BAE'>●</span> 空闲"),
                this);
    m_legendLabel->setObjectName(QStringLiteral("moduleOverviewLegend"));
    m_legendLabel->setTextFormat(Qt::RichText);
    headerLayout->addWidget(m_legendLabel, 0, Qt::AlignVCenter);

    rootLayout->addLayout(headerLayout);

    m_rowHost = new QWidget(this);
    m_rowHost->setObjectName(QStringLiteral("moduleRowHost"));
    m_rowHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_rowLayout = new QHBoxLayout(m_rowHost);
    m_rowLayout->setContentsMargins(0, 0, 0, 0);
    m_rowLayout->setSpacing(kModuleSpacing);
    m_rowLayout->addStretch(1);

    for (int i = 0; i < kMaxModules; ++i) {
        auto *rack = new ModuleRackWidget(i + 1, m_rowHost);
        m_modules[i] = rack;
        // 不使用 AlignCenter，否则布局会倾向保持 sizeHint 宽度，
        // 主窗口变窄时模组无法横向收缩。
        m_rowLayout->addWidget(rack);

        connect(rack, &ModuleRackWidget::curveRequested,
                this, [this](int moduleNumber, int localChannel) {
            openCurveDialog(moduleNumber, localChannel);
        });
    }

    m_rowLayout->addStretch(1);
    rootLayout->addWidget(m_rowHost, 1);

    m_hintLabel = new QLabel(this);
    m_hintLabel->setObjectName(QStringLiteral("moduleOverviewHint"));
    m_hintLabel->setWordWrap(true);
    rootLayout->addWidget(m_hintLabel);

    setStyleSheet(QStringLiteral(
        "QLabel#moduleOverviewTitle {"
        "  color: #1F4E79;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 20px;"
        "  font-weight: 700;"
        "}"
        "QLabel#moduleOverviewSubtitle {"
        "  color: #7A8FA3;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QLabel#globalTestStateBadge {"
        "  color: #7A8FA3;"
        "  background: #FFFFFF;"
        "  border: 1px solid #DCE7F1;"
        "  border-radius: 14px;"
        "  padding: 6px 14px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "}"
        "QLabel#globalTestStateBadge[state=\"active\"] {"
        "  color: #1E7B45;"
        "  background: #EDF8F1;"
        "  border: 1px solid #B8E0C8;"
        "}"
        "QLabel#globalTestStateBadge[state=\"idle\"] {"
        "  color: #7A8FA3;"
        "  background: #FFFFFF;"
        "  border: 1px solid #DCE7F1;"
        "}"
        "QLabel#moduleOverviewLegend {"
        "  color: #5A7086;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "  background: #FFFFFF;"
        "  border: 1px solid #DCE7F1;"
        "  border-radius: 14px;"
        "  padding: 6px 14px;"
        "}"
        "QLabel#moduleOverviewHint {"
        "  color: #6C8296;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "  background: #FFFFFF;"
        "  border: 1px solid #DCE7F1;"
        "  border-radius: 8px;"
        "  padding: 8px 12px;"
        "}"));

    connect(&TestSession::instance(), &TestSession::testingActiveChanged,
            this, &ModuleOverviewWidget::updateGlobalTestState);
    updateGlobalTestState(TestSession::instance().isTestingActive());

    setMachineModel(m_model);
}

void ModuleOverviewWidget::setMachineModel(MachineModel model)
{
    m_model = model;
    setActiveModuleCount(machineModuleCount(model));
}

MachineModel ModuleOverviewWidget::machineModel() const
{
    return m_model;
}

int ModuleOverviewWidget::activeModuleCount() const
{
    return m_activeCount;
}

void ModuleOverviewWidget::setActiveModuleCount(int count)
{
    const int value = qBound(1, count, kMaxModules);
    m_activeCount = value;

    for (int i = 0; i < kMaxModules; ++i) {
        if (m_modules[i]) {
            m_modules[i]->setVisible(i < value);
        }
    }

    updateHeaderText();
    updateLayoutSizes();
}

ModuleRackWidget *ModuleOverviewWidget::moduleRack(int moduleNumber) const
{
    if (moduleNumber < 1 || moduleNumber > kMaxModules) {
        return nullptr;
    }
    return m_modules[moduleNumber - 1];
}

bool ModuleOverviewWidget::isChannelEnabled(int moduleNumber,
                                            int localChannel) const
{
    ModuleRackWidget *rack = moduleRack(moduleNumber);
    return rack && rack->isChannelEnabled(localChannel);
}

ChannelRunState ModuleOverviewWidget::channelRunState(int moduleNumber,
                                                       int localChannel) const
{
    ModuleRackWidget *rack = moduleRack(moduleNumber);
    return rack ? rack->channelRunState(localChannel)
                : ChannelRunState::Disabled;
}

int ModuleOverviewWidget::channelAdValue(int moduleNumber,
                                        int localChannel) const
{
    ModuleRackWidget *rack = moduleRack(moduleNumber);
    return rack ? rack->channelAdValue(localChannel) : -1;
}

void ModuleOverviewWidget::setTestingActive(bool active)
{
    TestSession::instance().setTestingActive(active);
}

bool ModuleOverviewWidget::testingActive() const
{
    return TestSession::instance().isTestingActive();
}

void ModuleOverviewWidget::setTubeInserted(int moduleNumber, int localChannel,
                                           bool inserted, bool animated)
{
    if (ModuleRackWidget *rack = moduleRack(moduleNumber)) {
        rack->setTubeInserted(localChannel, inserted, animated);
    }
}

void ModuleOverviewWidget::setChannelProgress(int moduleNumber, int localChannel,
                                              int percent)
{
    if (ModuleRackWidget *rack = moduleRack(moduleNumber)) {
        rack->setChannelProgress(localChannel, percent);
    }
}

void ModuleOverviewWidget::setChannelStatus(int moduleNumber, int localChannel,
                                            const QString &status)
{
    if (ModuleRackWidget *rack = moduleRack(moduleNumber)) {
        rack->setChannelStatus(localChannel, status);
    }
}

void ModuleOverviewWidget::setChannelEnabled(int moduleNumber, int localChannel,
                                             bool enabled)
{
    if (ModuleRackWidget *rack = moduleRack(moduleNumber)) {
        rack->setChannelEnabled(localChannel, enabled);
    }
}

void ModuleOverviewWidget::setChannelRunState(int moduleNumber, int localChannel,
                                               ChannelRunState state)
{
    if (ModuleRackWidget *rack = moduleRack(moduleNumber)) {
        rack->setChannelRunState(localChannel, state);
    }
}

void ModuleOverviewWidget::setModuleTemperature(int moduleNumber,
                                                double celsius)
{
    if (ModuleRackWidget *rack = moduleRack(moduleNumber)) {
        rack->setModuleTemperature(celsius);
    }
}

void ModuleOverviewWidget::startMotor(int moduleNumber, int localChannel)
{
    if (ModuleRackWidget *rack = moduleRack(moduleNumber)) {
        rack->startMotor(localChannel);
    }
}

void ModuleOverviewWidget::stopMotor(int moduleNumber, int localChannel)
{
    if (ModuleRackWidget *rack = moduleRack(moduleNumber)) {
        rack->stopMotor(localChannel);
    }
}

void ModuleOverviewWidget::setMotorRunning(int moduleNumber, int localChannel,
                                           bool running)
{
    if (ModuleRackWidget *rack = moduleRack(moduleNumber)) {
        rack->setMotorRunning(localChannel, running);
    }
}

void ModuleOverviewWidget::setChannelTestInfo(int moduleNumber, int localChannel,
                                              const QString &sampleNumber,
                                              const QString &reagentName,
                                              int elapsedSeconds,
                                              int prpValue)
{
    if (ModuleRackWidget *rack = moduleRack(moduleNumber)) {
        ModuleRackWidget::ChannelTestInfo info;
        info.sampleNumber = sampleNumber;
        info.reagentName = reagentName;
        info.elapsedSeconds = elapsedSeconds;
        info.prpValue = prpValue;
        const ModuleRackWidget::ChannelTestInfo current =
                rack->channelTestInfo(localChannel);
        info.adValue = current.adValue;
        info.prp0Value = current.prp0Value;
        rack->setChannelTestInfo(localChannel, info);
    }
}

void ModuleOverviewWidget::setChannelAdValue(int moduleNumber, int localChannel,
                                             int adValue)
{
    if (ModuleRackWidget *rack = moduleRack(moduleNumber)) {
        rack->setChannelAdValue(localChannel, adValue);
    }
}

void ModuleOverviewWidget::appendChannelTestPoint(int moduleNumber,
                                                  int localChannel,
                                                  double seconds,
                                                  double value)
{
    if (ModuleRackWidget *rack = moduleRack(moduleNumber)) {
        rack->appendChannelTestPoint(localChannel, seconds, value);
    }
}

void ModuleOverviewWidget::appendChannelAggregationPoint(int moduleNumber,
                                                         int localChannel,
                                                         double seconds,
                                                         double value)
{
    if (ModuleRackWidget *rack = moduleRack(moduleNumber)) {
        rack->appendChannelAggregationPoint(localChannel, seconds, value);
    }
}

void ModuleOverviewWidget::clearChannelCurve(int moduleNumber, int localChannel)
{
    if (ModuleRackWidget *rack = moduleRack(moduleNumber)) {
        rack->clearChannelCurve(localChannel);
    }
}

void ModuleOverviewWidget::resetAll()
{
    for (int i = 0; i < kMaxModules; ++i) {
        if (m_modules[i]) {
            m_modules[i]->resetChannels();
        }
    }
    updateGlobalTestState(TestSession::instance().isTestingActive());
}

void ModuleOverviewWidget::updateHeaderText()
{
    if (m_subtitleLabel) {
        m_subtitleLabel->setText(
                    tr("%1 · %2 个模组 · %3 路通道")
                    .arg(QString::fromLatin1(machineModelName(m_model)))
                    .arg(m_activeCount)
                    .arg(m_activeCount * 4));
    }
}

void ModuleOverviewWidget::updateLayoutSizes()
{
    if (!m_rowHost) {
        return;
    }

    const int moduleHeight = qBound(kMinModuleHeight, m_rowHost->height(),
                                    kMaxModuleHeight);

    // 少模组机型在宽屏上适当放大；3 模组仍允许横向收缩，避免窄窗口被裁掉。
    const int count = qMax(1, m_activeCount);
    int preferredMinWidth = kMinModuleWidth;
    if (count == 1) {
        preferredMinWidth = kMaxModuleWidth;
    } else if (count == 2) {
        preferredMinWidth = 420;
    }

    for (int i = 0; i < kMaxModules; ++i) {
        if (!m_modules[i]) {
            continue;
        }
        // 宽度交给主界面的滚动区域/布局收缩，避免窗口变窄时模组被裁掉。
        // 高度仍按当前行高固定，保证内部绘制比例稳定。
        m_modules[i]->setMinimumWidth(preferredMinWidth);
        m_modules[i]->setMaximumWidth(kMaxModuleWidth);
        m_modules[i]->setFixedHeight(moduleHeight);
    }
}

void ModuleOverviewWidget::updateGlobalTestState(bool active)
{
    if (m_testStateLabel) {
        m_testStateLabel->setText(active ? tr("● 开始测试")
                                         : tr("● 未开始测试"));
        m_testStateLabel->setProperty("state", active ? "active" : "idle");
        m_testStateLabel->style()->unpolish(m_testStateLabel);
        m_testStateLabel->style()->polish(m_testStateLabel);
        m_testStateLabel->update();
    }
    if (m_hintLabel) {
        m_hintLabel->setText(active
                             ? tr("测试进行中：点击已插入试管的玻璃管，"
                                  "可查看该通道的实时曲线。")
                             : tr("未开始测试。测试开始后，点击已插入试管的"
                                  "玻璃管可查看该通道实时曲线。"));
    }
}

void ModuleOverviewWidget::openCurveDialog(int moduleNumber, int localChannel)
{
    ModuleRackWidget *rack = moduleRack(moduleNumber);
    if (!rack) {
        return;
    }

    if (m_curveDialog) {
        if (m_curveDialog->moduleNumber() == moduleNumber
                && m_curveDialog->localChannel() == localChannel
                && m_curveDialog->isVisible()) {
            m_curveDialog->raise();
            m_curveDialog->activateWindow();
            return;
        }
        m_curveDialog->close();
    }

    auto *dialog = new TestCurveDialog(rack, localChannel, this);
    m_curveDialog = dialog;
    if (QWidget *topWindow = window()) {
        const QRect topGeometry = topWindow->frameGeometry();
        dialog->move(topGeometry.center()
                     - QPoint(dialog->width() / 2, dialog->height() / 2));
    }
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void ModuleOverviewWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateLayoutSizes();
}

void ModuleOverviewWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updateLayoutSizes();
}
