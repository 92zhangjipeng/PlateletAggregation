#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "testcurvedialog.h"
#include "modulerackwidget.h"
#include "testcurvewidget.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QWidget>

namespace {

QString buildWindowTitle(ModuleRackWidget *rack, int localChannel)
{
    if (!rack) {
        return QObject::tr("测试曲线");
    }
    return QObject::tr("模组 %1 · CH%2 测试曲线")
            .arg(rack->moduleNumber())
            .arg(rack->globalChannelNumber(localChannel));
}

} // namespace

TestCurveDialog::TestCurveDialog(ModuleRackWidget *rack, int localChannel,
                                 QWidget *parent)
    : FramelessDialog(buildWindowTitle(rack, localChannel), parent)
    , m_rack(rack)
    , m_localChannel(qBound(1, localChannel, 4))
{
    setModal(false);
    setAttribute(Qt::WA_DeleteOnClose);
    resize(920, 620);
    setMinimumSize(760, 520);

    auto *content = new QWidget(this);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_curveWidget = new TestCurveWidget(content);
    layout->addWidget(m_curveWidget, 1);
    setContentWidget(content);

    m_moduleNumber = rack ? rack->moduleNumber() : 0;

    if (m_rack) {
        connect(m_rack.data(), &ModuleRackWidget::curveDataChanged,
                this, [this](int moduleNumber, int channel) {
            if (moduleNumber == m_moduleNumber
                    && channel == m_localChannel) {
                refreshFromRack();
            }
        });
        connect(m_rack.data(), &ModuleRackWidget::channelTestInfoChanged,
                this, [this](int moduleNumber, int channel) {
            if (moduleNumber == m_moduleNumber
                    && channel == m_localChannel) {
                refreshFromRack();
            }
        });
    }

    refreshFromRack();
}

int TestCurveDialog::moduleNumber() const
{
    return m_moduleNumber;
}

int TestCurveDialog::localChannel() const
{
    return m_localChannel;
}

void TestCurveDialog::refreshFromRack()
{
    if (!m_curveWidget) {
        return;
    }
    if (!m_rack) {
        m_curveWidget->setChannel(0, 0);
        m_curveWidget->clearCurves();
        return;
    }

    const ModuleRackWidget::ChannelTestInfo info =
            m_rack->channelTestInfo(m_localChannel);
    m_curveWidget->setChannel(m_moduleNumber,
                              m_rack->globalChannelNumber(m_localChannel));
    m_curveWidget->setHeaderInfo(info.sampleNumber, info.reagentName,
                                 info.elapsedSeconds, info.prpValue);
    m_curveWidget->setCurves(m_rack->testCurve(m_localChannel),
                             m_rack->aggregationCurve(m_localChannel));
}
