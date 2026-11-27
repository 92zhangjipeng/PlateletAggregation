#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "consumablesdialog.h"

#include <QDate>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QStringList>
#include <QStyle>
#include <QTime>
#include <QVariant>
#include <QVBoxLayout>

namespace {

QString mfcStatusText(quint8 status)
{
    switch (status) {
    case 0: return QObject::tr("正常");
    case 1: return QObject::tr("更新耗材总量");
    case 2: return QObject::tr("读卡成功");
    case 3: return QObject::tr("读卡失败");
    case 4: return QObject::tr("刷卡异常");
    case 5: return QObject::tr("充值成功");
    case 6: return QObject::tr("充值失败");
    case 7: return QObject::tr("全部写入 Flash");
    default: return QObject::tr("未知(%1)").arg(status);
    }
}

QString regTypeText(quint8 type)
{
    switch (type) {
    case 0: return QStringLiteral("AA");
    case 1: return QStringLiteral("EPI");
    case 2: return QStringLiteral("ADP");
    case 3: return QStringLiteral("COL");
    case 4: return QStringLiteral("RIS");
    case 5: return QObject::tr("内部清洗剂 S1");
    case 6: return QObject::tr("试管");
    case 7: return QObject::tr("外部清洗液 S2");
    default: return QObject::tr("未知(%1)").arg(type);
    }
}

QString consTypeText(quint8 type)
{
    switch (type) {
    case 0: return QStringLiteral("AA");
    case 1: return QStringLiteral("EPI");
    case 2: return QStringLiteral("ADP");
    case 3: return QStringLiteral("COL");
    case 4: return QStringLiteral("RIS");
    case 5: return QObject::tr("内部清洗剂");
    case 6: return QObject::tr("试管");
    case 7: return QStringLiteral("AA1");
    case 8: return QStringLiteral("EPI1");
    case 9: return QStringLiteral("ADP1");
    case 10: return QStringLiteral("COL1");
    case 11: return QStringLiteral("RIS1");
    case 12: return QObject::tr("无");
    default: return QObject::tr("未知(%1)").arg(type);
    }
}

QString consStatusText(quint8 status)
{
    switch (status) {
    case 0: return QObject::tr("默认");
    case 1: return QObject::tr("更新耗材余量");
    default: return QObject::tr("未知(%1)").arg(status);
    }
}

QLabel *makeDetailValue(QWidget *parent)
{
    auto *label = new QLabel(QStringLiteral("--"), parent);
    label->setObjectName(QStringLiteral("detailValue"));
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

void addDetailPair(QGridLayout *grid, int row, int column,
                   const QString &caption, QLabel *value, QWidget *parent)
{
    auto *captionLabel = new QLabel(caption, parent);
    captionLabel->setObjectName(QStringLiteral("detailCaption"));
    grid->addWidget(captionLabel, row, column,
                    Qt::AlignLeft | Qt::AlignVCenter);
    grid->addWidget(value, row, column + 1,
                    Qt::AlignLeft | Qt::AlignVCenter);
}

} // namespace

ConsumablesDialog::ConsumablesDialog(QWidget *parent)
    : FramelessDialog(tr("耗材管理"), parent)
{
    setModal(false);
    resize(900, 580);
    setMinimumSize(760, 540);

    setStyleSheet(QStringLiteral(
        "QDialog#framelessDialog {"
        "  background: #F4F8FC;"
        "  border: 1px solid #C5D4E4;"
        "}"
        "QWidget#consumablesContent { background: #F4F8FC; }"
        "QScrollArea#consumablesScroll {"
        "  background: #F4F8FC;"
        "  border: none;"
        "}"
        "QScrollArea#consumablesScroll > QWidget > QWidget {"
        "  background: #F4F8FC;"
        "}"
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
        "QLabel#sourceBadge {"
        "  color: #7D8F9E;"
        "  background: #F2F6F9;"
        "  border: 1px solid #D9E3EB;"
        "  border-radius: 11px;"
        "  padding: 3px 12px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "}"
        "QLabel#sourceBadge[state=\"live\"] {"
        "  color: #1E7B45;"
        "  background: #EDF8F1;"
        "  border-color: #BFE4CC;"
        "}"
        "QLabel#updatedLabel {"
        "  color: #93A4B3;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 11px;"
        "}"
        "QFrame#consumableStatCard, QFrame#detailCard {"
        "  background: #FFFFFF;"
        "  border: 1px solid #DCE7F1;"
        "  border-radius: 12px;"
        "}"
        "QLabel#statCaption {"
        "  color: #8296A9;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QLabel#statValue {"
        "  color: #1F4E79;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 23px;"
        "  font-weight: 700;"
        "}"
        "QLabel#statValue[state=\"error\"] { color: #C0392B; }"
        "QLabel#statSubValue {"
        "  color: #8A9CAB;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 11px;"
        "}"
        "QProgressBar#consumableProgress {"
        "  background: #EAF3F9;"
        "  border: 1px solid #D4E5F1;"
        "  border-radius: 5px;"
        "}"
        "QProgressBar#consumableProgress::chunk {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "      stop:0 #6CC6F2, stop:1 #2C7BBF);"
        "  border-radius: 4px;"
        "}"
        "QLabel#sectionTitle {"
        "  color: #1F4E79;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 15px;"
        "  font-weight: 600;"
        "}"
        "QLabel#sectionSubtitle {"
        "  color: #93A4B3;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 11px;"
        "}"
        "QLabel#detailCaption {"
        "  color: #8296A9;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QLabel#detailValue {"
        "  color: #1F2D3D;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 13px;"
        "  font-weight: 600;"
        "}"
        "QLabel#detailValue[state=\"warning\"] { color: #B7791F; }"
        "QLabel#detailValue[state=\"error\"] { color: #C0392B; }"
        "QLabel#detailValue[state=\"ok\"] { color: #1E7B45; }"
        "QPushButton#replaceButton {"
        "  background: #FFFFFF;"
        "  color: #1F4E79;"
        "  border: 1px solid #8FB4D6;"
        "  border-radius: 5px;"
        "  padding: 0px 10px;"
        "  min-height: 25px;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 12px;"
        "}"
        "QPushButton#replaceButton:hover { background: #E8F1FA; }"
        "QPushButton#replaceButton:pressed { background: #D3E6F5; }"
        "QPushButton#replaceButton:disabled {"
        "  background: #F5F8FA;"
        "  color: #A8B4BE;"
        "  border-color: #DCE7F1;"
        "}"
        "QFrame#noteCard {"
        "  background: #FFF9EC;"
        "  border: 1px solid #F0DFB6;"
        "  border-radius: 10px;"
        "}"
        "QLabel#noteTitle {"
        "  color: #8A6D3B;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 13px;"
        "  font-weight: 600;"
        "}"
        "QLabel#noteText {"
        "  color: #8A6D3B;"
        "  font-family: 'Microsoft YaHei';"
        "  font-size: 11px;"
        "}"));

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setObjectName(QStringLiteral("consumablesScroll"));
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setWidget(createContent());
    setContentWidget(scrollArea);
    clearStatus(tr("等待主板状态…"));
}

QWidget *ConsumablesDialog::createContent()
{
    auto *content = new QWidget;
    content->setObjectName(QStringLiteral("consumablesContent"));

    auto *root = new QVBoxLayout(content);
    root->setContentsMargins(24, 20, 24, 18);
    root->setSpacing(16);

    auto *header = new QHBoxLayout;
    header->setSpacing(16);

    auto *titleBox = new QVBoxLayout;
    titleBox->setSpacing(2);
    auto *title = new QLabel(tr("耗材管理"), content);
    title->setObjectName(QStringLiteral("pageTitle"));
    auto *subtitle = new QLabel(
                tr("主板耗材信息 · 每秒自动刷新"), content);
    subtitle->setObjectName(QStringLiteral("pageSubtitle"));
    titleBox->addWidget(title);
    titleBox->addWidget(subtitle);
    header->addLayout(titleBox);
    header->addStretch(1);

    auto *stateBox = new QVBoxLayout;
    stateBox->setSpacing(4);
    m_sourceBadge = new QLabel(tr("等待主板状态"), content);
    m_sourceBadge->setObjectName(QStringLiteral("sourceBadge"));
    m_sourceBadge->setProperty("state", "waiting");
    m_sourceBadge->setAlignment(Qt::AlignCenter);
    m_updatedLabel = new QLabel(tr("更新于 --"), content);
    m_updatedLabel->setObjectName(QStringLiteral("updatedLabel"));
    m_updatedLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    stateBox->addWidget(m_sourceBadge, 0, Qt::AlignRight);
    stateBox->addWidget(m_updatedLabel, 0, Qt::AlignRight);
    header->addLayout(stateBox);

    root->addLayout(header);

    auto *stats = new QGridLayout;
    stats->setContentsMargins(0, 0, 0, 0);
    stats->setHorizontalSpacing(12);
    stats->setVerticalSpacing(12);

    stats->addWidget(createStatCard(tr("耗材种类"), &m_consTypeValue,
                                    content), 0, 0);
    stats->addWidget(createRemainingCard(content), 0, 1);
    stats->addWidget(createStatCard(tr("试剂有效期"), &m_expiryValue,
                                    content), 0, 2);
    stats->addWidget(createStatCard(tr("刷卡瓶数"), &m_regRemainValue,
                                    content), 0, 3);
    for (int i = 0; i < 4; ++i) {
        stats->setColumnStretch(i, 1);
    }
    root->addLayout(stats);

    root->addWidget(createAmountCard(content));
    root->addWidget(createDetailCard(content), 1);

    auto *noteCard = new QFrame(content);
    noteCard->setObjectName(QStringLiteral("noteCard"));
    auto *noteLayout = new QVBoxLayout(noteCard);
    noteLayout->setContentsMargins(16, 10, 16, 10);
    noteLayout->setSpacing(3);
    auto *noteTitle = new QLabel(tr("数据说明"), noteCard);
    noteTitle->setObjectName(QStringLiteral("noteTitle"));
    auto *noteText = new QLabel(
                tr("状态信息来自主板状态帧（地址 0x05 / 功能码 0x16 / 命令 0xFE），"
                   "每秒自动刷新；各试剂剩余总量由主板逐个读取命令（100~112）获得，"
                   "试剂/清洗液单位为瓶，试管单位为板。\n"
                   "更换 AA~RIS 左/右试剂瓶：测试机械臂运动或未联机时禁止；"
                   "连续两次确认后从总瓶数扣 1 瓶，并把该侧单瓶补满 512。\n"
                   "刷卡充值：读到卡后软件自动暂停主板轮询并立即写入，"
                   "写入结果见「充值状态」；写入期间请勿移开卡片。"),
                noteCard);
    noteText->setObjectName(QStringLiteral("noteText"));
    noteText->setWordWrap(true);
    noteLayout->addWidget(noteTitle);
    noteLayout->addWidget(noteText);
    root->addWidget(noteCard);

    return content;
}

QWidget *ConsumablesDialog::createStatCard(const QString &caption,
                                           QLabel **valueLabel,
                                           QWidget *parent)
{
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("consumableStatCard"));
    card->setMinimumHeight(124);

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(4);

    auto *captionLabel = new QLabel(caption, card);
    captionLabel->setObjectName(QStringLiteral("statCaption"));
    auto *value = new QLabel(QStringLiteral("--"), card);
    value->setObjectName(QStringLiteral("statValue"));
    value->setTextInteractionFlags(Qt::TextSelectableByMouse);

    layout->addWidget(captionLabel);
    layout->addWidget(value);
    layout->addStretch(1);

    if (valueLabel) {
        *valueLabel = value;
    }
    return card;
}

QWidget *ConsumablesDialog::createRemainingCard(QWidget *parent)
{
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("consumableStatCard"));
    card->setMinimumHeight(124);

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(4);

    auto *caption = new QLabel(tr("耗材余量"), card);
    caption->setObjectName(QStringLiteral("statCaption"));

    m_remainPercentValue = new QLabel(QStringLiteral("--"), card);
    m_remainPercentValue->setObjectName(QStringLiteral("statValue"));
    m_remainPercentValue->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_remainBar = new QProgressBar(card);
    m_remainBar->setObjectName(QStringLiteral("consumableProgress"));
    m_remainBar->setRange(0, 512);
    m_remainBar->setValue(0);
    m_remainBar->setTextVisible(false);
    m_remainBar->setFixedHeight(9);

    m_remainDetailValue = new QLabel(QStringLiteral("-- / 512"), card);
    m_remainDetailValue->setObjectName(QStringLiteral("statSubValue"));

    layout->addWidget(caption);
    layout->addWidget(m_remainPercentValue);
    layout->addWidget(m_remainBar);
    layout->addWidget(m_remainDetailValue);
    layout->addStretch(1);

    return card;
}

QWidget *ConsumablesDialog::createAmountCard(QWidget *parent)
{
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("detailCard"));

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(20, 16, 20, 18);
    layout->setSpacing(12);

    auto *titleRow = new QHBoxLayout;
    titleRow->setSpacing(12);
    auto *title = new QLabel(tr("各试剂剩余总量"), card);
    title->setObjectName(QStringLiteral("sectionTitle"));
    auto *subtitle = new QLabel(
                tr("主板逐个读取 · 试剂/清洗液单位：瓶 · 试管单位：板"), card);
    subtitle->setObjectName(QStringLiteral("sectionSubtitle"));
    titleRow->addWidget(title);
    titleRow->addWidget(subtitle, 0, Qt::AlignBottom);
    titleRow->addStretch(1);
    layout->addLayout(titleRow);

    auto *grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(18);
    grid->setVerticalSpacing(10);

    const QStringList headers = {
        tr("试剂 / 耗材"), tr("剩余总量"),
        tr("单瓶余量（左）"), tr("单瓶余量（右）"),
        tr("左瓶操作"), tr("右瓶操作")
    };
    for (int column = 0; column < headers.size(); ++column) {
        auto *header = new QLabel(headers.at(column), card);
        header->setObjectName(QStringLiteral("detailCaption"));
        grid->addWidget(header, 0, column);
    }

    const auto addRow = [this, card, grid](int row, const QString &name,
                                           AmountRow amountRow,
                                           int leftPosition,
                                           int rightPosition) {
        auto *nameLabel = new QLabel(name, card);
        nameLabel->setObjectName(QStringLiteral("detailValue"));
        grid->addWidget(nameLabel, row, 0);

        m_amountValues[amountRow] = makeDetailValue(card);
        m_amountLeftValues[amountRow] = makeDetailValue(card);
        m_amountRightValues[amountRow] = makeDetailValue(card);
        grid->addWidget(m_amountValues[amountRow], row, 1);
        grid->addWidget(m_amountLeftValues[amountRow], row, 2);
        grid->addWidget(m_amountRightValues[amountRow], row, 3);

        const auto addReplaceButton = [this, card, grid, row](
                int position, int column) {
            auto *button = new QPushButton(tr("更换"), card);
            button->setObjectName(QStringLiteral("replaceButton"));
            button->setCursor(Qt::PointingHandCursor);
            button->setMinimumWidth(62);
            button->setFixedHeight(26);
            button->setEnabled(m_replaceAllowed);
            button->setToolTip(
                        m_replaceAllowed
                        ? tr("更换 %1：扣除 1 瓶总库存并把该侧补满")
                          .arg(MainBoardConsumable::positionDisplayName(position))
                        : (m_replaceDisabledReason.isEmpty()
                           ? tr("当前不可更换") : m_replaceDisabledReason));
            m_replaceButtons[position] = button;
            connect(button, &QPushButton::clicked, this,
                    [this, position]() {
                emit reagentReplaceRequested(position);
            });
            grid->addWidget(button, row, column);
        };

        if (leftPosition >= 0) {
            addReplaceButton(leftPosition, 4);
        }
        if (rightPosition >= 0) {
            addReplaceButton(rightPosition, 5);
        }
    };

    addRow(1, tr("AA 试剂"), AmountAA,
           MainBoardConsumable::AA, MainBoardConsumable::AA_Second);
    addRow(2, tr("EPI 试剂"), AmountEPI,
           MainBoardConsumable::EPI, MainBoardConsumable::EPI_Second);
    addRow(3, tr("ADP 试剂"), AmountADP,
           MainBoardConsumable::ADP, MainBoardConsumable::ADP_Second);
    addRow(4, tr("COL 试剂"), AmountCOL,
           MainBoardConsumable::COL, MainBoardConsumable::COL_Second);
    addRow(5, tr("RIS 试剂"), AmountRIS,
           MainBoardConsumable::RIS, MainBoardConsumable::RIS_Second);
    addRow(6, tr("内部清洗剂 S1"), AmountInternalClean, -1, -1);
    addRow(7, tr("试管"), AmountTestTube, -1, -1);
    addRow(8, tr("外部清洗液 S2"), AmountExternalClean, -1, -1);

    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);
    grid->setColumnStretch(3, 1);
    layout->addLayout(grid);

    return card;
}

QWidget *ConsumablesDialog::createDetailCard(QWidget *parent)
{
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("detailCard"));

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(20, 16, 20, 18);
    layout->setSpacing(12);

    auto *titleRow = new QHBoxLayout;
    titleRow->setSpacing(12);
    auto *title = new QLabel(tr("主板耗材明细"), card);
    title->setObjectName(QStringLiteral("sectionTitle"));
    auto *subtitle = new QLabel(
                tr("刷卡 / 余量 / 有效期 / 读卡器状态"), card);
    subtitle->setObjectName(QStringLiteral("sectionSubtitle"));
    titleRow->addWidget(title);
    titleRow->addWidget(subtitle, 0, Qt::AlignBottom);
    titleRow->addStretch(1);
    layout->addLayout(titleRow);

    auto *grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(18);
    grid->setVerticalSpacing(10);

    m_detailMfc = makeDetailValue(card);
    m_detailRegType = makeDetailValue(card);
    m_detailRegRemain = makeDetailValue(card);
    m_detailConsStatus = makeDetailValue(card);
    m_detailConsType = makeDetailValue(card);
    m_detailConsumable = makeDetailValue(card);
    m_detailExpiry = makeDetailValue(card);
    m_detailUpdated = makeDetailValue(card);
    m_detailRecharge = makeDetailValue(card);
    m_detailRecharge->setText(tr("等待刷卡…"));

    addDetailPair(grid, 0, 0, tr("读卡器 MFC"), m_detailMfc, card);
    addDetailPair(grid, 0, 2, tr("刷卡类型"), m_detailRegType, card);
    addDetailPair(grid, 1, 0, tr("刷卡瓶数"), m_detailRegRemain, card);
    addDetailPair(grid, 1, 2, tr("耗材余量状态"), m_detailConsStatus, card);
    addDetailPair(grid, 2, 0, tr("耗材种类"), m_detailConsType, card);
    addDetailPair(grid, 2, 2, tr("耗材余量"), m_detailConsumable, card);
    addDetailPair(grid, 3, 0, tr("试剂有效期"), m_detailExpiry, card);
    addDetailPair(grid, 3, 2, tr("最近更新"), m_detailUpdated, card);
    addDetailPair(grid, 4, 0, tr("充值状态"), m_detailRecharge, card);

    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(3, 1);
    layout->addLayout(grid);
    layout->addStretch(1);

    return card;
}

QString ConsumablesDialog::expiryText(quint8 year, quint8 month,
                                      bool *expired)
{
    if (expired) {
        *expired = false;
    }
    if (year == 0 || month == 0 || month > 12) {
        return QStringLiteral("--");
    }

    const int fullYear = 2000 + year;

    const QDate expiryDate(fullYear, month, 1);
    if (!expiryDate.isValid()) {
        return QStringLiteral("--");
    }

    const QDate current = QDate::currentDate();
    const QDate currentMonth(current.year(), current.month(), 1);
    if (expired) {
        *expired = expiryDate < currentMonth;
    }

    return tr("%1 年 %2 月")
            .arg(fullYear)
            .arg(month, 2, 10, QLatin1Char('0'));
}

void ConsumablesDialog::setLabelState(QLabel *label, const char *state)
{
    if (!label) {
        return;
    }
    const QString stateText = QString::fromLatin1(state);
    if (label->property("state").toString() == stateText) {
        return;
    }
    label->setProperty("state", stateText);
    label->style()->unpolish(label);
    label->style()->polish(label);
    label->update();
}

void ConsumablesDialog::setMainboardStatus(const MainBoardStatus &status)
{
    if (!status.valid) {
        clearStatus(tr("等待有效的主板状态…"));
        return;
    }

    const int percent = qBound(0, qRound(status.consumable / 512.0 * 100.0),
                               100);
    const QString timeText = QTime::currentTime().toString(
                QStringLiteral("HH:mm:ss"));

    m_consTypeValue->setText(consTypeText(status.consType));
    setLabelState(m_consTypeValue, "");

    m_remainPercentValue->setText(QStringLiteral("%1%").arg(percent));
    m_remainBar->setValue(qBound(0, static_cast<int>(status.consumable), 512));
    m_remainDetailValue->setText(tr("%1 / 512").arg(status.consumable));

    bool expiryExpired = false;
    const QString expiry = expiryText(status.regYear, status.regMonth,
                                      &expiryExpired);
    m_expiryValue->setText(expiry);
    setLabelState(m_expiryValue, expiryExpired ? "error" : "");

    m_regRemainValue->setText(tr("%1 瓶").arg(status.regRemain));
    setLabelState(m_regRemainValue, "");

    m_detailMfc->setText(mfcStatusText(status.mfcStatus));
    const bool mfcIssue = status.mfcStatus == 3
            || status.mfcStatus == 4 || status.mfcStatus == 6;
    setLabelState(m_detailMfc, mfcIssue ? "error" : "");

    m_detailRegType->setText(regTypeText(status.regType));
    m_detailRegRemain->setText(tr("%1 瓶").arg(status.regRemain));
    m_detailConsStatus->setText(consStatusText(status.consStatus));
    m_detailConsType->setText(consTypeText(status.consType));
    const double exactPercent = status.consumable / 512.0 * 100.0;
    m_detailConsumable->setText(
                tr("%1 / 512（%2%）")
                .arg(status.consumable)
                .arg(exactPercent, 0, 'f', 1));
    m_detailExpiry->setText(expiry);
    setLabelState(m_detailExpiry, expiryExpired ? "error" : "");
    m_detailUpdated->setText(tr("实时更新 · %1").arg(timeText));

    m_sourceBadge->setText(tr("实时数据"));
    setLabelState(m_sourceBadge, "live");
    m_updatedLabel->setText(tr("更新于 %1").arg(timeText));
}

void ConsumablesDialog::setConsumableAmount(int position, int bottles,
                                            int ratio)
{
    if (position < 0 || position >= MainBoardConsumable::kPositionCount) {
        return;
    }

    m_positionBottles[position] = bottles;
    m_positionRatios[position] = ratio;
    m_positionValid[position] = bottles >= 0;
    updateConsumableAmountLabels();
}

void ConsumablesDialog::clearConsumableAmounts()
{
    for (int i = 0; i < MainBoardConsumable::kPositionCount; ++i) {
        m_positionBottles[i] = -1;
        m_positionRatios[i] = -1;
        m_positionValid[i] = false;
    }
    updateConsumableAmountLabels();
}

void ConsumablesDialog::updateConsumableAmountLabels()
{
    const auto ratioPercent = [](int ratio) -> QString {
        if (ratio < 0) {
            return QStringLiteral("--");
        }
        const double percent = qBound(0, ratio, MainBoardConsumable::kRatioMax)
                * 100.0 / MainBoardConsumable::kRatioMax;
        return ConsumablesDialog::tr("%1%").arg(percent, 0, 'f', 0);
    };
    // S1 容量大、单次扣减少，整数百分比看不出变化，单独用 1 位小数。
    const auto ratioPercent1 = [](int ratio) -> QString {
        if (ratio < 0) {
            return QStringLiteral("--");
        }
        const double percent = qBound(0, ratio, MainBoardConsumable::kRatioMax)
                * 100.0 / MainBoardConsumable::kRatioMax;
        return ConsumablesDialog::tr("%1%").arg(percent, 0, 'f', 1);
    };

    const auto setText = [](QLabel *label, const QString &text,
                            const QString &toolTip) {
        if (!label) {
            return;
        }
        label->setText(text);
        label->setToolTip(toolTip);
    };

    const auto setReagentRow = [this, &ratioPercent, &setText](
            AmountRow row, int mainPosition, int rightPosition) {
        const bool mainValid = m_positionValid[mainPosition];
        const bool rightValid = m_positionValid[rightPosition];

        setText(m_amountValues[row],
                mainValid
                ? ConsumablesDialog::tr("%1 瓶")
                  .arg(m_positionBottles[mainPosition])
                : QStringLiteral("--"),
                ConsumablesDialog::tr("该试剂剩余总量（瓶）"));
        setText(m_amountLeftValues[row],
                mainValid
                ? ratioPercent(m_positionRatios[mainPosition])
                : QStringLiteral("--"),
                ConsumablesDialog::tr("左侧单瓶剩余百分比"));
        setText(m_amountRightValues[row],
                rightValid
                ? ratioPercent(m_positionRatios[rightPosition])
                : QStringLiteral("--"),
                ConsumablesDialog::tr("右侧单瓶剩余百分比"));
    };

    setReagentRow(AmountAA, MainBoardConsumable::AA,
                  MainBoardConsumable::AA_Second);
    setReagentRow(AmountEPI, MainBoardConsumable::EPI,
                  MainBoardConsumable::EPI_Second);
    setReagentRow(AmountADP, MainBoardConsumable::ADP,
                  MainBoardConsumable::ADP_Second);
    setReagentRow(AmountCOL, MainBoardConsumable::COL,
                  MainBoardConsumable::COL_Second);
    setReagentRow(AmountRIS, MainBoardConsumable::RIS,
                  MainBoardConsumable::RIS_Second);

    const bool s1Valid = m_positionValid[MainBoardConsumable::InternalCleanS1];
    setText(m_amountValues[AmountInternalClean],
            s1Valid
            ? ConsumablesDialog::tr("%1 瓶")
              .arg(m_positionBottles[MainBoardConsumable::InternalCleanS1])
            : QStringLiteral("--"),
            ConsumablesDialog::tr("内部清洗液 S1 剩余总量（瓶）"));
    setText(m_amountLeftValues[AmountInternalClean],
            s1Valid
            ? ratioPercent1(
                  m_positionRatios[MainBoardConsumable::InternalCleanS1])
            : QStringLiteral("--"),
            ConsumablesDialog::tr("当前瓶剩余百分比"));
    setText(m_amountRightValues[AmountInternalClean],
            QStringLiteral("--"), QString());

    const bool tubeValid = m_positionValid[MainBoardConsumable::TestTube];
    setText(m_amountValues[AmountTestTube],
            tubeValid
            ? ConsumablesDialog::tr("%1 板")
              .arg(m_positionBottles[MainBoardConsumable::TestTube])
            : QStringLiteral("--"),
            ConsumablesDialog::tr("试管剩余总量（板）"));
    setText(m_amountLeftValues[AmountTestTube],
            QStringLiteral("--"), QString());
    setText(m_amountRightValues[AmountTestTube],
            QStringLiteral("--"), QString());

    const bool s2Valid = m_positionValid[MainBoardConsumable::ExternalCleanS2];
    setText(m_amountValues[AmountExternalClean],
            s2Valid
            ? ConsumablesDialog::tr("%1 瓶")
              .arg(m_positionBottles[MainBoardConsumable::ExternalCleanS2])
            : QStringLiteral("--"),
            ConsumablesDialog::tr("外部清洗液 S2 剩余总量（瓶）"));
    setText(m_amountLeftValues[AmountExternalClean],
            s2Valid
            ? ratioPercent(
                  m_positionRatios[MainBoardConsumable::ExternalCleanS2])
            : QStringLiteral("--"),
            ConsumablesDialog::tr("当前瓶剩余百分比"));
    setText(m_amountRightValues[AmountExternalClean],
            QStringLiteral("--"), QString());
}

void ConsumablesDialog::setReplaceAllowed(bool allowed, const QString &reason)
{
    m_replaceAllowed = allowed;
    m_replaceDisabledReason = reason;
    for (int position = 0; position < MainBoardConsumable::kPositionCount;
         ++position) {
        QPushButton *button = m_replaceButtons[position];
        if (!button) {
            continue;
        }
        button->setEnabled(allowed);
        button->setToolTip(
                    allowed
                    ? tr("更换 %1：扣除 1 瓶总库存并把该侧补满")
                      .arg(MainBoardConsumable::positionDisplayName(position))
                    : (reason.isEmpty() ? tr("当前不可更换") : reason));
    }
}

void ConsumablesDialog::setRechargeStatus(const QString &text, bool ok)
{
    if (!m_detailRecharge) {
        return;
    }
    m_detailRecharge->setText(text);
    m_detailRecharge->setProperty("state", ok ? "ok" : "error");
    m_detailRecharge->style()->unpolish(m_detailRecharge);
    m_detailRecharge->style()->polish(m_detailRecharge);
    m_detailRecharge->update();
}

void ConsumablesDialog::clearStatus(const QString &reason)
{
    const QString text = reason.isEmpty()
            ? tr("等待主板状态…") : reason;

    m_sourceBadge->setText(tr("等待主板状态"));
    setLabelState(m_sourceBadge, "waiting");
    m_updatedLabel->setText(text);

    const QString placeholder = QStringLiteral("--");
    m_consTypeValue->setText(placeholder);
    setLabelState(m_consTypeValue, "");
    m_remainPercentValue->setText(placeholder);
    m_remainBar->setValue(0);
    m_remainDetailValue->setText(QStringLiteral("-- / 512"));
    m_expiryValue->setText(placeholder);
    setLabelState(m_expiryValue, "");
    m_regRemainValue->setText(placeholder);

    m_detailMfc->setText(placeholder);
    m_detailRegType->setText(placeholder);
    m_detailRegRemain->setText(placeholder);
    m_detailConsStatus->setText(placeholder);
    m_detailConsType->setText(placeholder);
    m_detailConsumable->setText(placeholder);
    m_detailExpiry->setText(placeholder);
    m_detailUpdated->setText(text);

    setLabelState(m_detailMfc, "");
    setLabelState(m_detailExpiry, "");
}
