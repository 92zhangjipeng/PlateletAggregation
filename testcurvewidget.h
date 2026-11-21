#ifndef TESTCURVEWIDGET_H
#define TESTCURVEWIDGET_H

#include <QPointF>
#include <QString>
#include <QVector>
#include <QWidget>

class QButtonGroup;
class QCheckBox;
class QLabel;
class CurvePlotWidget;

/**
 * 单通道实时曲线控件：
 * - 顶部：样本号、试剂、时间、PRP 和 AD 信息（例如“样本0001 AA 300S PRP=2300”）；
 * - 中部：自绘实时曲线；
 * - 底部：互斥的「测试曲线」和「集聚曲线」两个 QCheckBox。
 *
 * 数据通过 setCurves()/appendPoint() 接入；x 轴单位为秒，
 * 测试曲线 y 值为 AD，集聚曲线 y 值为百分比。
 */
class TestCurveWidget : public QWidget
{
    Q_OBJECT

public:
    enum class CurveMode {
        TestCurve = 0,
        AggregationCurve = 1
    };

    explicit TestCurveWidget(QWidget *parent = nullptr);

    void setChannel(int moduleNumber, int globalChannel);

    /** 曲线窗口顶部信息；无效值传 -1 或空字符串会显示为“--” */
    void setHeaderInfo(const QString &sampleNumber,
                       const QString &reagentName,
                       int elapsedSeconds,
                       int prpValue);

    void setCurves(const QVector<QPointF> &testCurve,
                   const QVector<QPointF> &aggregationCurve);
    /** 同时追加两条曲线的一个点；不需要某条曲线时传 -1 */
    void appendPoint(double seconds, double testValue, double aggregationValue);
    void clearCurves();

    void setCurveMode(CurveMode mode);
    CurveMode curveMode() const;

signals:
    void curveModeChanged(TestCurveWidget::CurveMode mode);

private:
    void updateHeaderText();
    void updateModeChecks();
    void refreshPlot();
    void updateDataState();

    QLabel *m_infoLabel = nullptr;
    QLabel *m_channelLabel = nullptr;
    QLabel *m_dataStateLabel = nullptr;
    QCheckBox *m_testCheck = nullptr;
    QCheckBox *m_aggregationCheck = nullptr;
    QButtonGroup *m_modeGroup = nullptr;
    CurvePlotWidget *m_plot = nullptr;

    QString m_sampleNumber;
    QString m_reagentName;
    int m_elapsedSeconds = 0;
    int m_prpValue = -1;
    int m_moduleNumber = 0;
    int m_globalChannel = 0;

    CurveMode m_mode = CurveMode::TestCurve;
    QVector<QPointF> m_testCurve;
    QVector<QPointF> m_aggregationCurve;
};

#endif // TESTCURVEWIDGET_H
