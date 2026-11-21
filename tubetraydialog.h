#ifndef TUBETRAYDIALOG_H
#define TUBETRAYDIALOG_H

#include "FramelessDialog.h"

#include <QPoint>
#include <QVector>

class QLabel;
class QTabWidget;
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

private slots:
    void refresh();

private:
    void handleTubeClicked(int tubeIndex, const QPoint &globalPos);
    void clearCurrentTray();
    void clearAllTrays();

    SampleTrayModel *m_model = nullptr;
    QTabWidget *m_tabs = nullptr;
    QVector<TrayGridWidget *> m_grids;
    QLabel *m_subtitleLabel = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QLabel *m_hintLabel = nullptr;
};

#endif // TUBETRAYDIALOG_H
