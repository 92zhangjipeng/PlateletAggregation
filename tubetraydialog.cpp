#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "tubetraydialog.h"
#include "sampletraymodel.h"

#include <QAction>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QStyle>
#include <QTabWidget>
#include <QToolTip>
#include <QVBoxLayout>

#include <functional>
#include <cmath>

namespace {

QColor stateFill(SampleTrayModel::TubeState state)
{
    switch (state) {
    case SampleTrayModel::TubeState::Free: return QColor("#F4F8FB");
    case SampleTrayModel::TubeState::Allocated: return QColor("#FFF4D9");
    case SampleTrayModel::TubeState::Sampled: return QColor("#E6F4FE");
    case SampleTrayModel::TubeState::Used: return QColor("#EAF8F1");
    }
    return QColor("#F4F8FB");
}

QColor stateBorder(SampleTrayModel::TubeState state)
{
    switch (state) {
    case SampleTrayModel::TubeState::Free: return QColor("#D7E4EF");
    case SampleTrayModel::TubeState::Allocated: return QColor("#E7C979");
    case SampleTrayModel::TubeState::Sampled: return QColor("#A6CCE8");
    case SampleTrayModel::TubeState::Used: return QColor("#A9D8BC");
    }
    return QColor("#D7E4EF");
}

QColor stateTextColor(SampleTrayModel::TubeState state)
{
    switch (state) {
    case SampleTrayModel::TubeState::Free: return QColor("#6B8194");
    case SampleTrayModel::TubeState::Allocated: return QColor("#966A16");
    case SampleTrayModel::TubeState::Sampled: return QColor("#1F6FA5");
    case SampleTrayModel::TubeState::Used: return QColor("#1E7B45");
    }
    return QColor("#6B8194");
}

} // namespace

class TrayGridWidget : public QWidget
{
public:
    using ClickHandler = std::function<void(int, const QPoint &)>;

    TrayGridWidget(SampleTrayModel *model, int trayIndex, QWidget *parent)
        : QWidget(parent)
        , m_model(model)
        , m_trayIndex(trayIndex)
    {
        setMouseTracking(true);
        setMinimumSize(620, 420);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setActive(bool active)
    {
        if (m_active == active) {
            return;
        }
        m_active = active;
        update();
    }

    bool isActive() const
    {
        return m_active;
    }

    void setClickHandler(const ClickHandler &handler)
    {
        m_clickHandler = handler;
    }

    int trayIndex() const
    {
        return m_trayIndex;
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::TextAntialiasing, true);

        const QRectF card = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
        p.setPen(QPen(QColor("#DCE7F1"), 1.0));
        p.setBrush(QColor("#FFFFFF"));
        p.drawRoundedRect(card, 12.0, 12.0);

        QFont titleFont(QStringLiteral("Microsoft YaHei"));
        titleFont.setPixelSize(14);
        titleFont.setBold(true);
        p.setFont(titleFont);
        p.setPen(QColor("#1F4E79"));
        const int startId = m_trayIndex * SampleTrayModel::TubesPerTray;
        const int endId = startId + SampleTrayModel::TubesPerTray - 1;
        p.drawText(QRectF(card.left() + 16.0, card.top() + 8.0,
                          240.0, 22.0),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   tr("试管盘 %1").arg(m_trayIndex + 1));

        QFont subFont(QStringLiteral("Microsoft YaHei"));
        subFont.setPixelSize(10);
        p.setFont(subFont);
        p.setPen(QColor("#8A9CAB"));
        p.drawText(QRectF(card.left() + 104.0, card.top() + 8.0,
                          card.width() - 120.0, 22.0),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   tr("编号 %1–%2").arg(startId).arg(endId));

        if (!m_active) {
            p.setPen(QColor("#A9B9C6"));
            QFont inactiveFont(QStringLiteral("Microsoft YaHei"));
            inactiveFont.setPixelSize(18);
            inactiveFont.setBold(true);
            p.setFont(inactiveFont);
            p.drawText(card, Qt::AlignCenter, tr("当前机型未使用此试管盘"));
            return;
        }

        const qreal rowHeaderWidth = 24.0;
        const qreal columnHeaderHeight = 18.0;
        const QRectF area(card.left() + 42.0,
                          card.top() + 44.0,
                          card.width() - 58.0,
                          card.height() - 58.0);
        const QRectF grid(area.left() + rowHeaderWidth,
                          area.top() + columnHeaderHeight,
                          area.width() - rowHeaderWidth,
                          area.height() - columnHeaderHeight);
        if (grid.width() < 300.0 || grid.height() < 200.0) {
            return;
        }

        const qreal cellWidth = grid.width() / SampleTrayModel::ColumnsPerTray;
        const qreal cellHeight = grid.height() / SampleTrayModel::RowsPerTray;

        QFont headerFont(QStringLiteral("Microsoft YaHei"));
        headerFont.setPixelSize(9);
        p.setFont(headerFont);
        p.setPen(QColor("#8A9CAB"));
        for (int column = 0; column < SampleTrayModel::ColumnsPerTray;
             ++column) {
            const QRectF headerRect(grid.left() + column * cellWidth,
                                    grid.top() - columnHeaderHeight,
                                    cellWidth, columnHeaderHeight);
            p.drawText(headerRect, Qt::AlignCenter,
                       QString::number(column + 1));
        }
        for (int row = 0; row < SampleTrayModel::RowsPerTray; ++row) {
            const QRectF headerRect(grid.left() - rowHeaderWidth,
                                    grid.top() + row * cellHeight,
                                    rowHeaderWidth - 4.0, cellHeight);
            p.drawText(headerRect, Qt::AlignCenter,
                       QString::number(row + 1));
        }

        QFont idFont(QStringLiteral("Microsoft YaHei"));
        idFont.setPixelSize(10);
        idFont.setBold(true);
        QFont stateFont(QStringLiteral("Microsoft YaHei"));
        stateFont.setPixelSize(8);

        for (int row = 0; row < SampleTrayModel::RowsPerTray; ++row) {
            for (int column = 0;
                 column < SampleTrayModel::ColumnsPerTray; ++column) {
                const int tubeIndex = SampleTrayModel::tubeIndex(
                            m_trayIndex, row, column);
                if (tubeIndex < 0) {
                    continue;
                }
                const SampleTrayModel::TubeInfo info =
                        m_model->tubeInfo(tubeIndex);
                const QRectF cell(grid.left() + column * cellWidth + 2.0,
                                  grid.top() + row * cellHeight + 2.0,
                                  cellWidth - 4.0, cellHeight - 4.0);
                if (cell.width() < 12.0 || cell.height() < 12.0) {
                    continue;
                }

                const bool hovered = tubeIndex == m_hoverIndex;
                QColor border = stateBorder(info.state);
                if (hovered) {
                    border = QColor("#2C7BBF");
                }

                // 试管俯视图：玻璃管圆环（外圈 + 内圈 + 高光弧）
                const qreal diameter = qMax<qreal>(
                            8.0, qMin(cell.width(), cell.height()) - 6.0);
                const QRectF ring(cell.center().x() - diameter / 2.0,
                                  cell.center().y() - diameter / 2.0,
                                  diameter, diameter);

                QColor fill = stateFill(info.state);
                fill.setAlpha(190);
                p.setPen(QPen(border, hovered ? 2.2 : 1.4));
                p.setBrush(fill);
                p.drawEllipse(ring);

                p.setBrush(Qt::NoBrush);
                p.setPen(QPen(QColor(255, 255, 255, 210), 1.0));
                p.drawEllipse(ring.adjusted(2.5, 2.5, -2.5, -2.5));

                p.setPen(QPen(QColor(255, 255, 255, 235), 1.3));
                p.drawArc(ring.adjusted(4.0, 4.0, -4.0, -4.0),
                          35 * 16, 100 * 16);

                // 管号画在圆环中心
                p.setFont(idFont);
                p.setPen(stateTextColor(info.state));
                p.drawText(ring.adjusted(2.0, 2.0, -2.0, -2.0),
                           Qt::AlignCenter, QString::number(tubeIndex));

                // 圆环下方还有空间时显示状态文字
                if (cell.height() - diameter >= 13.0) {
                    QFont smallStateFont = stateFont;
                    p.setFont(smallStateFont);
                    QColor stateColor = stateTextColor(info.state);
                    stateColor.setAlpha(205);
                    p.setPen(stateColor);
                    p.drawText(QRectF(cell.left(), cell.bottom() - 13.0,
                                      cell.width(), 12.0),
                               Qt::AlignHCenter | Qt::AlignBottom,
                               SampleTrayModel::stateText(info.state));
                }
            }
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const int index = tubeAt(event->pos());
        if (index != m_hoverIndex) {
            m_hoverIndex = index;
            if (index >= 0 && m_active) {
                const SampleTrayModel::TubeInfo info =
                        m_model->tubeInfo(index);
                QString text = tr("试管 %1\n试管盘 %2 · 第 %3 行第 %4 列\n状态：%5")
                        .arg(index)
                        .arg(SampleTrayModel::trayOf(index) + 1)
                        .arg(SampleTrayModel::rowOf(index) + 1)
                        .arg(SampleTrayModel::columnOf(index) + 1)
                        .arg(SampleTrayModel::stateText(info.state));
                if (!info.sampleNumber.isEmpty()) {
                    text += tr("\n样本：%1").arg(info.sampleNumber);
                }
                if (!info.reagent.isEmpty()) {
                    text += tr("\n试剂：%1").arg(info.reagent);
                }
                if (info.updatedAt.isValid()) {
                    text += tr("\n更新：%1")
                            .arg(info.updatedAt.toString(
                                     QStringLiteral("yyyy-MM-dd HH:mm:ss")));
                }
                QToolTip::showText(event->globalPos(), text, this);
            } else {
                QToolTip::hideText();
            }
            update();
        }
        QWidget::mouseMoveEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        m_hoverIndex = -1;
        QToolTip::hideText();
        update();
        QWidget::leaveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (m_active && event->button() == Qt::LeftButton) {
            const int index = tubeAt(event->pos());
            if (index >= 0 && m_clickHandler) {
                m_clickHandler(index, event->globalPos());
            }
        }
        QWidget::mouseReleaseEvent(event);
    }

private:
    QRectF gridRect() const
    {
        const QRectF card = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
        const QRectF area(card.left() + 42.0,
                          card.top() + 44.0,
                          card.width() - 58.0,
                          card.height() - 58.0);
        return QRectF(area.left() + 24.0,
                      area.top() + 18.0,
                      area.width() - 24.0,
                      area.height() - 18.0);
    }

    int tubeAt(const QPoint &pos) const
    {
        if (!m_active) {
            return -1;
        }
        const QRectF grid = gridRect();
        if (!grid.contains(QPointF(pos))) {
            return -1;
        }
        const qreal cellWidth = grid.width() / SampleTrayModel::ColumnsPerTray;
        const qreal cellHeight = grid.height() / SampleTrayModel::RowsPerTray;
        if (cellWidth <= 1.0 || cellHeight <= 1.0) {
            return -1;
        }
        const int column = int((pos.x() - grid.left()) / cellWidth);
        const int row = int((pos.y() - grid.top()) / cellHeight);
        return SampleTrayModel::tubeIndex(m_trayIndex, row, column);
    }

    SampleTrayModel *m_model = nullptr;
    int m_trayIndex = 0;
    bool m_active = true;
    int m_hoverIndex = -1;
    ClickHandler m_clickHandler;
};

TubeTrayDialog::TubeTrayDialog(SampleTrayModel *model, QWidget *parent)
    : FramelessDialog(tr("试管盘状态"), parent)
    , m_model(model)
{
    setModal(false);
    resize(940, 680);
    setMinimumSize(820, 600);

    setStyleSheet(QStringLiteral(
        "QDialog#framelessDialog {"
        "  background: #F4F8FC;"
        "  border: 1px solid #C5D4E4;"
        "}"
        "QWidget#tubeTrayContent { background: #F4F8FC; }"
        "QLabel#pageTitle {"
        "  color: #1F4E79;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 21px;"
        "  font-weight: 700;"
        "}"
        "QLabel#pageSubtitle {"
        "  color: #7A8FA3;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QLabel#traySummary {"
        "  color: #5D7386;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 13px;"
        "}"
        "QLabel#trayLegend {"
        "  color: #61788B;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 11px;"
        "}"
        "QTabWidget::pane {"
        "  border: 1px solid #DCE7F1;"
        "  border-radius: 10px;"
        "  background: #F8FBFD;"
        "  top: -1px;"
        "}"
        "QTabBar::tab {"
        "  background: #EAF2F8;"
        "  color: #5A7086;"
        "  border: 1px solid #D4E3EF;"
        "  border-bottom: none;"
        "  border-top-left-radius: 6px;"
        "  border-top-right-radius: 6px;"
        "  padding: 7px 18px;"
        "  margin-right: 2px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QTabBar::tab:selected {"
        "  background: #FFFFFF;"
        "  color: #1F4E79;"
        "  font-weight: 600;"
        "}"
        "QTabBar::tab:disabled { color: #AFC0CC; background: #F2F6F9; }"
        "QPushButton#trayActionButton {"
        "  background: #FFFFFF;"
        "  color: #1F4E79;"
        "  border: 1px solid #A9C8DF;"
        "  border-radius: 6px;"
        "  padding: 5px 14px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QPushButton#trayActionButton:hover { background: #E8F1FA; }"
        "QPushButton#trayActionButton:pressed { background: #D3E6F5; }"
        "QLabel#trayHint {"
        "  color: #8A9CAB;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 11px;"
        "}"));

    auto *content = new QWidget;
    content->setObjectName(QStringLiteral("tubeTrayContent"));
    auto *root = new QVBoxLayout(content);
    root->setContentsMargins(20, 16, 20, 14);
    root->setSpacing(10);

    auto *header = new QHBoxLayout;
    auto *titleBox = new QVBoxLayout;
    titleBox->setSpacing(2);
    auto *title = new QLabel(tr("试管盘状态"), content);
    title->setObjectName(QStringLiteral("pageTitle"));
    m_subtitleLabel = new QLabel(content);
    m_subtitleLabel->setObjectName(QStringLiteral("pageSubtitle"));
    titleBox->addWidget(title);
    titleBox->addWidget(m_subtitleLabel);
    header->addLayout(titleBox);
    header->addStretch(1);

    m_summaryLabel = new QLabel(content);
    m_summaryLabel->setObjectName(QStringLiteral("traySummary"));
    m_summaryLabel->setTextFormat(Qt::RichText);
    m_summaryLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    header->addWidget(m_summaryLabel, 0, Qt::AlignVCenter);
    root->addLayout(header);

    m_tabs = new QTabWidget(content);
    m_tabs->setDocumentMode(true);
    m_tabs->setTabPosition(QTabWidget::North);
    for (int tray = 0; tray < SampleTrayModel::MaxTrays; ++tray) {
        auto *grid = new TrayGridWidget(m_model, tray, m_tabs);
        grid->setClickHandler([this](int index, const QPoint &globalPos) {
            handleTubeClicked(index, globalPos);
        });
        m_grids.append(grid);
        const int start = tray * SampleTrayModel::TubesPerTray;
        m_tabs->addTab(grid, tr("试管盘 %1  (%2–%3)")
                       .arg(tray + 1)
                       .arg(start)
                       .arg(start + SampleTrayModel::TubesPerTray - 1));
    }
    root->addWidget(m_tabs, 1);

    auto *legend = new QLabel(
                tr("<span style='color:#6B8194;'>■</span> 空闲&nbsp;&nbsp;"
                   "<span style='color:#966A16;'>■</span> 分配&nbsp;&nbsp;"
                   "<span style='color:#1F6FA5;'>■</span> 已加样&nbsp;&nbsp;"
                   "<span style='color:#1E7B45;'>■</span> 已使用"),
                content);
    legend->setObjectName(QStringLiteral("trayLegend"));
    legend->setTextFormat(Qt::RichText);
    root->addWidget(legend);

    auto *footer = new QHBoxLayout;
    m_hintLabel = new QLabel(
                tr("维护提示：点击试管可打开状态维护菜单；正常测试流程应通过 "
                   "SampleTrayModel 接口自动维护。"),
                content);
    m_hintLabel->setObjectName(QStringLiteral("trayHint"));
    footer->addWidget(m_hintLabel, 1);

    auto *clearTrayButton = new QPushButton(tr("清空当前盘"), content);
    clearTrayButton->setObjectName(QStringLiteral("trayActionButton"));
    clearTrayButton->setCursor(Qt::PointingHandCursor);
    connect(clearTrayButton, &QPushButton::clicked,
            this, &TubeTrayDialog::clearCurrentTray);
    footer->addWidget(clearTrayButton);

    auto *clearAllButton = new QPushButton(tr("全部清空"), content);
    clearAllButton->setObjectName(QStringLiteral("trayActionButton"));
    clearAllButton->setCursor(Qt::PointingHandCursor);
    connect(clearAllButton, &QPushButton::clicked,
            this, &TubeTrayDialog::clearAllTrays);
    footer->addWidget(clearAllButton);

    root->addLayout(footer);
    setContentWidget(content);

    if (m_model) {
        connect(m_model, &SampleTrayModel::modelChanged,
                this, &TubeTrayDialog::refresh);
    }
    refresh();
}

void TubeTrayDialog::refresh()
{
    if (!m_model) {
        return;
    }

    const int activeTrays = m_model->activeTrayCount();
    const int activeTubes = activeTrays * SampleTrayModel::TubesPerTray;
    m_subtitleLabel->setText(
                tr("%1 · %2 个试管盘 · 每盘 %3 行 × %4 列 = %5 支 · 共 %6 支")
                .arg(QString::fromLatin1(
                         machineModelName(m_model->machineModel())))
                .arg(activeTrays)
                .arg(SampleTrayModel::RowsPerTray)
                .arg(SampleTrayModel::ColumnsPerTray)
                .arg(SampleTrayModel::TubesPerTray)
                .arg(activeTubes));

    m_summaryLabel->setText(
                tr("<span style='color:#6B8194;'>空闲 %1</span>"
                   "&nbsp;&nbsp;<span style='color:#966A16;'>分配 %2</span>"
                   "&nbsp;&nbsp;<span style='color:#1F6FA5;'>已加样 %3</span>"
                   "&nbsp;&nbsp;<span style='color:#1E7B45;'>已使用 %4</span>")
                .arg(m_model->countState(SampleTrayModel::TubeState::Free))
                .arg(m_model->countState(SampleTrayModel::TubeState::Allocated))
                .arg(m_model->countState(SampleTrayModel::TubeState::Sampled))
                .arg(m_model->countState(SampleTrayModel::TubeState::Used)));

    for (int tray = 0; tray < m_grids.size(); ++tray) {
        const bool active = tray < activeTrays;
        m_grids[tray]->setActive(active);
        m_tabs->setTabEnabled(tray, active);
        m_tabs->setTabToolTip(
                    tray, active ? tr("可用") : tr("当前机型未使用"));
        m_grids[tray]->update();
    }
    if (m_tabs->currentIndex() >= activeTrays) {
        m_tabs->setCurrentIndex(0);
    }
}

void TubeTrayDialog::handleTubeClicked(int tubeIndex,
                                       const QPoint &globalPos)
{
    if (!m_model || !SampleTrayModel::isValidTubeIndex(tubeIndex)) {
        return;
    }
    const SampleTrayModel::TubeInfo info = m_model->tubeInfo(tubeIndex);
    const SampleTrayModel::TubeState state = info.state;

    QMenu menu(this);
    QAction *header = menu.addAction(
                tr("试管 %1 · 第 %2 行第 %3 列 · %4")
                .arg(tubeIndex)
                .arg(SampleTrayModel::rowOf(tubeIndex) + 1)
                .arg(SampleTrayModel::columnOf(tubeIndex) + 1)
                .arg(SampleTrayModel::stateText(state)));
    header->setEnabled(false);
    if (!info.sampleNumber.isEmpty()) {
        QAction *sample = menu.addAction(
                    tr("样本：%1").arg(info.sampleNumber));
        sample->setEnabled(false);
    }
    menu.addSeparator();

    QAction *allocateAction = nullptr;
    QAction *sampleAction = nullptr;
    QAction *usedAction = nullptr;
    QAction *clearAction = nullptr;

    switch (state) {
    case SampleTrayModel::TubeState::Free:
        allocateAction = menu.addAction(tr("分配样本…"));
        break;
    case SampleTrayModel::TubeState::Allocated:
        sampleAction = menu.addAction(tr("标记为已加样"));
        clearAction = menu.addAction(tr("释放为空闲"));
        break;
    case SampleTrayModel::TubeState::Sampled:
        usedAction = menu.addAction(tr("标记为已使用"));
        clearAction = menu.addAction(tr("释放为空闲"));
        break;
    case SampleTrayModel::TubeState::Used:
        clearAction = menu.addAction(tr("回收为空闲"));
        break;
    }

    QAction *selected = menu.exec(globalPos);
    if (!selected) {
        return;
    }

    QString error;
    bool ok = false;
    if (selected == allocateAction) {
        bool accepted = false;
        const QString sampleNumber = QInputDialog::getText(
                    this, tr("分配试管"),
                    tr("试管 %1 的样本号：").arg(tubeIndex),
                    QLineEdit::Normal, QString(), &accepted).trimmed();
        if (!accepted) {
            return;
        }
        if (sampleNumber.isEmpty()) {
            QMessageBox::warning(this, tr("分配试管"),
                                 tr("样本号不能为空。"));
            return;
        }
        ok = m_model->allocateTube(tubeIndex, sampleNumber, &error);
    } else if (selected == sampleAction) {
        ok = m_model->markSampled(tubeIndex, QString(), &error);
    } else if (selected == usedAction) {
        ok = m_model->markUsed(tubeIndex, &error);
    } else if (selected == clearAction) {
        ok = m_model->clearTube(tubeIndex, &error);
    }

    if (!ok && !error.isEmpty()) {
        QMessageBox::warning(this, tr("试管状态维护"), error);
    }
}

void TubeTrayDialog::clearCurrentTray()
{
    if (!m_model || !m_tabs) {
        return;
    }
    const int tray = m_tabs->currentIndex();
    if (tray < 0 || tray >= m_model->activeTrayCount()) {
        return;
    }
    const QMessageBox::StandardButton answer = QMessageBox::question(
                this, tr("清空试管盘"),
                tr("确定将试管盘 %1 的 60 支试管全部重置为空闲吗？")
                .arg(tray + 1),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer == QMessageBox::Yes) {
        m_model->clearTray(tray);
    }
}

void TubeTrayDialog::clearAllTrays()
{
    if (!m_model) {
        return;
    }
    const QMessageBox::StandardButton answer = QMessageBox::question(
                this, tr("清空全部试管盘"),
                tr("确定将当前机型使用的所有试管全部重置为空闲吗？"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer == QMessageBox::Yes) {
        m_model->clearAll();
    }
}
