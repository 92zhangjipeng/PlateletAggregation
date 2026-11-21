#ifndef TUBETRAYDIALOG_H
#define TUBETRAYDIALOG_H

#include "FramelessDialog.h"

#include <QPoint>
#include <QVector>

class QHBoxLayout;
class QLabel;
class SampleTrayModel;
class TrayGridWidget;

/**
 * 试管盘状态维护界面：
 * 4 个 Tab，每个 Tab 显示一个 10×6 的试管盘（最多 240 支试管），
 * 颜色区分空闲 / 分配 / 已加样 / 已使用，点击试管可维护状态。
 */
class TubeTrayDialog : public FramelessDialog
{
    Q_OBJECT

public:
    explicit TubeTrayDialog(SampleTrayModel *model,
                            QWidget *parent = nullptr);

signals:
    /** 请求更换某盘：由主窗口检查测试状态、二次确认并更新主板余量 */
    void trayReplaceRequested(int trayIndex);

private slots:
    void refresh();

private:
    void handleTubeClicked(int tubeIndex, const QPoint &globalPos);
    /** 按当前机型盘数重建水平排列的试管盘 */
    void rebuildTrayWidgets();

    SampleTrayModel *m_model = nullptr;
    QWidget *m_trayRow = nullptr;
    QHBoxLayout *m_trayLayout = nullptr;
    QVector<TrayGridWidget *> m_grids;
    QLabel *m_subtitleLabel = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QLabel *m_hintLabel = nullptr;
};

#endif // TUBETRAYDIALOG_H
