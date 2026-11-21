#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "resultstablewidget.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>

namespace {

constexpr int kMargin = 14;
constexpr int kHeaderHeight = 38;
constexpr int kRowHeight = 34;

// 列宽比例：样本号 / 患者 / 类型 / 试剂数 / 时间 / 机型
constexpr double kColumnRatios[] = {
    0.18, 0.17, 0.11, 0.09, 0.27, 0.18
};
constexpr int kColumnCount = 6;

QString columnText(const SampleSummary &row, int column)
{
    switch (column) {
    case 0: return row.sampleNumber.isEmpty()
                ? QStringLiteral("--") : row.sampleNumber;
    case 1: return row.patientName.isEmpty()
                ? QStringLiteral("--") : row.patientName;
    case 2: return row.sampleType.isEmpty()
                ? QStringLiteral("--") : row.sampleType;
    case 3: return QString::number(row.testCount);
    case 4: return row.testTime.isValid()
                ? row.testTime.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                : QStringLiteral("--");
    case 5: return row.machineModel.isEmpty()
                ? QStringLiteral("--") : row.machineModel;
    default: return QString();
    }
}

} // namespace

ResultsTableWidget::ResultsTableWidget(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("resultsTableWidget"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(true);
    setMinimumHeight(kMargin * 2 + kHeaderHeight + kRowHeight);
}

void ResultsTableWidget::setRows(const QVector<SampleSummary> &rows)
{
    m_rows = rows;
    setMinimumHeight(kMargin * 2 + kHeaderHeight
                     + qMax(1, m_rows.size()) * kRowHeight);
    updateGeometry();
    update();
}

int ResultsTableWidget::rowCount() const
{
    return m_rows.size();
}

QSize ResultsTableWidget::sizeHint() const
{
    return QSize(900, minimumHeight());
}

int ResultsTableWidget::rowAt(int y) const
{
    const int top = kMargin + kHeaderHeight;
    if (y < top) {
        return -1;
    }
    const int row = (y - top) / kRowHeight;
    return (row >= 0 && row < m_rows.size()) ? row : -1;
}

void ResultsTableWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const QRectF card = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
    p.setPen(QPen(QColor("#DCE7F1"), 1.0));
    p.setBrush(QColor("#FFFFFF"));
    p.drawRoundedRect(card, 10.0, 10.0);

    const qreal tableLeft = card.left() + kMargin;
    const qreal tableWidth = card.width() - kMargin * 2.0;
    if (tableWidth < 200.0) {
        return;
    }

    qreal weights[kColumnCount] = {};
    double totalWeight = 0.0;
    for (int i = 0; i < kColumnCount; ++i) {
        totalWeight += kColumnRatios[i];
    }
    for (int i = 0; i < kColumnCount; ++i) {
        weights[i] = tableWidth * kColumnRatios[i] / totalWeight;
    }

    const QRectF headerRect(tableLeft, card.top() + kMargin,
                            tableWidth, kHeaderHeight);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#EAF3FA"));
    p.drawRoundedRect(headerRect, 7.0, 7.0);

    const QStringList headers = {
        tr("样本号"), tr("患者"), tr("样本类型"),
        tr("试剂数"), tr("检测时间"), tr("机型")
    };

    QFont headerFont(QStringLiteral("Microsoft YaHei"));
    headerFont.setPixelSize(12);
    headerFont.setBold(true);
    p.setFont(headerFont);
    p.setPen(QColor("#1F4E79"));

    qreal x = tableLeft + 12.0;
    for (int column = 0; column < kColumnCount; ++column) {
        const QRectF rect(x, headerRect.top(),
                          weights[column] - 18.0, headerRect.height());
        p.drawText(rect, Qt::AlignLeft | Qt::AlignVCenter,
                   headers.at(column));
        x += weights[column];
    }

    if (m_rows.isEmpty()) {
        QFont emptyFont(QStringLiteral("Microsoft YaHei"));
        emptyFont.setPixelSize(13);
        p.setFont(emptyFont);
        p.setPen(QColor("#8A9CAB"));
        p.drawText(QRectF(tableLeft,
                          headerRect.bottom() + 14.0,
                          tableWidth,
                          kRowHeight * 2.0),
                   Qt::AlignHCenter | Qt::AlignTop,
                   tr("暂无测试结果"));
        return;
    }

    QFont cellFont(QStringLiteral("Microsoft YaHei"));
    cellFont.setPixelSize(12);
    const QFontMetrics cellMetrics(cellFont);

    const QRectF gridRect(tableLeft, headerRect.bottom(),
                          tableWidth,
                          m_rows.size() * kRowHeight);
    p.setPen(QPen(QColor("#E6EEF5"), 1.0));
    p.setBrush(Qt::NoBrush);
    qreal gridX = tableLeft;
    for (int column = 0; column < kColumnCount; ++column) {
        p.drawLine(QPointF(gridX, gridRect.top()),
                   QPointF(gridX, gridRect.bottom()));
        gridX += weights[column];
    }
    p.drawLine(QPointF(tableLeft, gridRect.bottom()),
               QPointF(tableLeft + tableWidth, gridRect.bottom()));

    for (int row = 0; row < m_rows.size(); ++row) {
        const QRectF rowRect(tableLeft,
                             headerRect.bottom() + row * kRowHeight,
                             tableWidth, kRowHeight);
        if (row % 2 == 1) {
            p.fillRect(rowRect, QColor("#F8FBFD"));
        }
        if (row > 0) {
            p.setPen(QPen(QColor("#E6EEF5"), 1.0));
            p.drawLine(QPointF(tableLeft, rowRect.top()),
                       QPointF(tableLeft + tableWidth, rowRect.top()));
        }

        p.setFont(cellFont);
        p.setPen(QColor("#30475C"));
        qreal cellX = tableLeft + 12.0;
        for (int column = 0; column < kColumnCount; ++column) {
            const QString text = cellMetrics.elidedText(
                        columnText(m_rows.at(row), column),
                        Qt::ElideRight,
                        qMax(10, qRound(weights[column] - 20.0)));
            const QRectF textRect(cellX, rowRect.top(),
                                  weights[column] - 20.0, rowRect.height());
            p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text);
            cellX += weights[column];
        }
    }
}

void ResultsTableWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const int row = rowAt(event->pos().y());
        if (row >= 0 && row < m_rows.size()) {
            emit rowActivated(m_rows.at(row).id);
        }
    }
    QWidget::mouseReleaseEvent(event);
}
