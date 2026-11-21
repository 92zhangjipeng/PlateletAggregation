#ifndef TESTCURVEDIALOG_H
#define TESTCURVEDIALOG_H

#include "FramelessDialog.h"

#include <QPointer>

class ModuleRackWidget;
class TestCurveWidget;

/**
 * 单通道测试曲线弹窗：内容为 TestCurveWidget，并实时跟随
 * ModuleRackWidget 的曲线/头部信息变化刷新。
 */
class TestCurveDialog : public FramelessDialog
{
    Q_OBJECT

public:
    TestCurveDialog(ModuleRackWidget *rack, int localChannel,
                    QWidget *parent = nullptr);

    int moduleNumber() const;
    int localChannel() const;

private:
    void refreshFromRack();

    QPointer<ModuleRackWidget> m_rack;
    TestCurveWidget *m_curveWidget = nullptr;
    int m_moduleNumber = 0;
    int m_localChannel = 1;
};

#endif // TESTCURVEDIALOG_H
