/**
 * @file    AlarmPanel.cpp
 * @brief   报警面板实现
 */

#include "AlarmPanel.h"

#include "AlarmStore.h"
#include "LedIndicator.h"

#include <QBrush>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
const QColor kCriticalColor(0xC0, 0x39, 0x2B);   // 严重：深红
const QColor kWarningColor(0xE6, 0x7E, 0x22);    // 警告：橙
const QColor kInfoColor(0x7F, 0x8C, 0x8D);       // 提示：灰
const QColor kOkColor(0x27, 0xAE, 0x60);         // 恢复/正常：绿

QColor levelColor(AlarmLevel l)
{
    switch (l) {
    case AlarmLevel::Critical: return kCriticalColor;
    case AlarmLevel::Warning:  return kWarningColor;
    default:                   return kInfoColor;
    }
}
}

AlarmPanel::AlarmPanel(AlarmStore *store, QWidget *parent)
    : QGroupBox(QStringLiteral("阈值报警"), parent)
    , m_store(store)
{
    setupUi();
    reloadHistory();
}

void AlarmPanel::setupUi()
{
    // ===== 当前状态 =====
    m_led = new LedIndicator;
    m_led->setFixedSize(16, 16);
    m_led->setState(LedIndicator::On);
    m_lblState = new QLabel(QStringLiteral("正常"));
    m_lblState->setStyleSheet(QStringLiteral("color: %1; font-weight: bold;").arg(kOkColor.name()));
    m_lblState->setWordWrap(true);

    QHBoxLayout *stateRow = new QHBoxLayout;
    stateRow->addWidget(m_led);
    stateRow->addWidget(m_lblState, 1);

    // ===== 阈值配置 =====
    m_chkEnabled = new QCheckBox(QStringLiteral("启用报警"));
    m_chkEnabled->setChecked(true);

    m_chkTemp = new QCheckBox(QStringLiteral("温度"));
    m_chkTemp->setChecked(true);
    m_spinTempLow = new QDoubleSpinBox;
    m_spinTempLow->setRange(-20.0, 120.0);
    m_spinTempLow->setDecimals(1);
    m_spinTempLow->setSuffix(QStringLiteral(" °C"));
    m_spinTempLow->setValue(5.0);
    m_spinTempHigh = new QDoubleSpinBox;
    m_spinTempHigh->setRange(-20.0, 120.0);
    m_spinTempHigh->setDecimals(1);
    m_spinTempHigh->setSuffix(QStringLiteral(" °C"));
    m_spinTempHigh->setValue(35.0);

    m_chkHum = new QCheckBox(QStringLiteral("湿度"));
    m_chkHum->setChecked(false);
    m_spinHumLow = new QDoubleSpinBox;
    m_spinHumLow->setRange(0.0, 100.0);
    m_spinHumLow->setDecimals(1);
    m_spinHumLow->setSuffix(QStringLiteral(" %"));
    m_spinHumLow->setValue(20.0);
    m_spinHumHigh = new QDoubleSpinBox;
    m_spinHumHigh->setRange(0.0, 100.0);
    m_spinHumHigh->setDecimals(1);
    m_spinHumHigh->setSuffix(QStringLiteral(" %"));
    m_spinHumHigh->setValue(90.0);

    m_spinDebounce = new QSpinBox;
    m_spinDebounce->setRange(1, 50);
    m_spinDebounce->setValue(3);
    m_spinDebounce->setToolTip(QStringLiteral("连续多少帧越限才触发/恢复，用于抑制 DHT11 抖动造成的误报"));

    m_btnApply = new QPushButton(QStringLiteral("应用阈值"));

    QGridLayout *cfg = new QGridLayout;
    cfg->addWidget(m_chkEnabled, 0, 0);
    cfg->addWidget(m_btnApply, 0, 2);
    cfg->addWidget(m_chkTemp, 1, 0);
    cfg->addWidget(m_spinTempLow, 1, 1);
    cfg->addWidget(m_spinTempHigh, 1, 2);
    cfg->addWidget(m_chkHum, 2, 0);
    cfg->addWidget(m_spinHumLow, 2, 1);
    cfg->addWidget(m_spinHumHigh, 2, 2);
    cfg->addWidget(new QLabel(QStringLiteral("去抖帧数:")), 3, 0);
    cfg->addWidget(m_spinDebounce, 3, 1);

    // ===== 历史报警表格 =====
    m_table = new QTableWidget(0, ColCount);
    m_table->setHorizontalHeaderLabels({QStringLiteral("ID"), QStringLiteral("时间"), QStringLiteral("通道"),
                                        QStringLiteral("等级"), QStringLiteral("类型"), QStringLiteral("实测值"),
                                        QStringLiteral("阈值"), QStringLiteral("说明"), QStringLiteral("确认")});
    m_table->setColumnHidden(ColId, true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setMinimumHeight(160);

    m_btnAck = new QPushButton(QStringLiteral("确认选中"));
    m_btnAckAll = new QPushButton(QStringLiteral("全部确认"));
    QPushButton *btnRefresh = new QPushButton(QStringLiteral("刷新"));
    m_btnClear = new QPushButton(QStringLiteral("清空历史"));
    m_lblCount = new QLabel(QStringLiteral("共 0 条"));
    m_lblCount->setStyleSheet(QStringLiteral("color: #7f8c8d;"));

    QHBoxLayout *tableBar = new QHBoxLayout;
    tableBar->addWidget(m_lblCount);
    tableBar->addStretch(1);
    tableBar->addWidget(m_btnAck);
    tableBar->addWidget(m_btnAckAll);
    tableBar->addWidget(btnRefresh);
    tableBar->addWidget(m_btnClear);

    QVBoxLayout *main = new QVBoxLayout(this);
    main->addLayout(stateRow);
    main->addLayout(cfg);
    main->addWidget(m_table, 1);
    main->addLayout(tableBar);

    connect(m_btnApply, &QPushButton::clicked, this, &AlarmPanel::slotApply);
    connect(m_btnAck, &QPushButton::clicked, this, &AlarmPanel::slotAckSelected);
    connect(m_btnAckAll, &QPushButton::clicked, this, &AlarmPanel::slotAckAll);
    connect(m_btnClear, &QPushButton::clicked, this, &AlarmPanel::slotClear);
    connect(btnRefresh, &QPushButton::clicked, this, &AlarmPanel::reloadHistory);
}

void AlarmPanel::setPermissions(bool canConfigure, bool canPurge)
{
    m_canConfigure = canConfigure;
    m_canPurge = canPurge;

    // 访客只读：阈值配置区与确认按钮整体禁用
    // 用显式数组而非初始化列表，避免 MSVC 对混合派生类型的 auto 推导失败
    QWidget *const configurable[] = {
        m_chkEnabled, m_chkTemp, m_chkHum,
        m_spinTempLow, m_spinTempHigh, m_spinHumLow, m_spinHumHigh,
        m_spinDebounce, m_btnApply, m_btnAck, m_btnAckAll
    };
    for (QWidget *w : configurable)
        w->setEnabled(canConfigure);

    m_btnClear->setEnabled(canPurge);   // 清空历史仅管理员可用
}

bool AlarmPanel::collectThresholds(AlarmThresholds *out) const
{
    AlarmThresholds t;
    t.enabled = m_chkEnabled->isChecked();
    t.tempEnabled = m_chkTemp->isChecked();
    t.tempLow = m_spinTempLow->value();
    t.tempHigh = m_spinTempHigh->value();
    t.humEnabled = m_chkHum->isChecked();
    t.humLow = m_spinHumLow->value();
    t.humHigh = m_spinHumHigh->value();
    t.debounceFrames = m_spinDebounce->value();

    if (!t.isValid()) {
        QMessageBox::warning(const_cast<AlarmPanel *>(this), QStringLiteral("阈值非法"),
                             QStringLiteral("下限必须小于上限，去抖帧数至少为 1。"));
        return false;
    }
    *out = t;
    return true;
}

void AlarmPanel::slotApply()
{
    AlarmThresholds t;
    if (!collectThresholds(&t))
        return;

    emit sigThresholdsChanged(t);
    emit sigAudit(QStringLiteral("ALARM_THRESHOLD"),
                  QStringLiteral("温度[%1,%2]°C 湿度[%3,%4]% 去抖%5帧 启用=%6")
                      .arg(t.tempLow, 0, 'f', 1).arg(t.tempHigh, 0, 'f', 1)
                      .arg(t.humLow, 0, 'f', 1).arg(t.humHigh, 0, 'f', 1)
                      .arg(t.debounceFrames)
                      .arg(t.enabled ? QStringLiteral("是") : QStringLiteral("否")));
}

void AlarmPanel::slotThresholdsChanged(const AlarmThresholds &t)
{
    // 以引擎实际接受的配置为准回填，避免界面显示与被拒绝的非法值不一致
    m_chkEnabled->setChecked(t.enabled);
    m_chkTemp->setChecked(t.tempEnabled);
    m_spinTempLow->setValue(t.tempLow);
    m_spinTempHigh->setValue(t.tempHigh);
    m_chkHum->setChecked(t.humEnabled);
    m_spinHumLow->setValue(t.humLow);
    m_spinHumHigh->setValue(t.humHigh);
    m_spinDebounce->setValue(t.debounceFrames);
}

void AlarmPanel::slotActiveChanged(bool active, const QString &summary)
{
    m_led->setState(active ? LedIndicator::Error : LedIndicator::On);
    if (active) {
        m_lblState->setText(summary.isEmpty() ? QStringLiteral("报警中") : summary);
        m_lblState->setStyleSheet(QStringLiteral("color: %1; font-weight: bold;").arg(kCriticalColor.name()));
    } else {
        m_lblState->setText(QStringLiteral("正常"));
        m_lblState->setStyleSheet(QStringLiteral("color: %1; font-weight: bold;").arg(kOkColor.name()));
    }
}

void AlarmPanel::slotAlarm(const AlarmEvent &ev)
{
    Q_UNUSED(ev);
    // 事件已由主窗口写入数据库，这里统一重新查询，保证表格与库内容一致
    reloadHistory();
}

void AlarmPanel::reloadHistory()
{
    if (!m_store)
        return;

    const QList<AlarmEvent> events = m_store->query(500);
    m_table->setRowCount(0);
    for (const AlarmEvent &e : events)
        appendRow(e);
    m_table->resizeColumnsToContents();

    const int unacked = m_store->unackedCount();
    m_lblCount->setText(QStringLiteral("共 %1 条，未确认 %2 条").arg(events.size()).arg(unacked));
}

void AlarmPanel::appendRow(const AlarmEvent &ev)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    setRowWidgets(row, ev);
}

void AlarmPanel::setRowWidgets(int row, const AlarmEvent &ev)
{
    auto *idItem = new QTableWidgetItem(QString::number(ev.id));
    idItem->setData(Qt::UserRole, ev.id);
    m_table->setItem(row, ColId, idItem);

    const QColor color = (ev.kind == AlarmKind::Recovered) ? kOkColor : levelColor(ev.level);

    auto make = [&color](const QString &text) {
        auto *item = new QTableWidgetItem(text);
        item->setForeground(QBrush(color));
        return item;
    };

    m_table->setItem(row, ColTime, make(ev.ts.toString(QStringLiteral("MM-dd hh:mm:ss"))));
    m_table->setItem(row, ColChannel, make(AlarmText::channel(ev.channel)));
    m_table->setItem(row, ColLevel, make(AlarmText::level(ev.level)));
    m_table->setItem(row, ColKind, make(AlarmText::kind(ev.kind)));
    m_table->setItem(row, ColValue, make(QStringLiteral("%1%2")
                                             .arg(ev.value, 0, 'f', 2)
                                             .arg(AlarmText::unit(ev.channel))));
    m_table->setItem(row, ColThreshold, make(QStringLiteral("%1%2")
                                                 .arg(ev.threshold, 0, 'f', 2)
                                                 .arg(AlarmText::unit(ev.channel))));
    m_table->setItem(row, ColMessage, make(ev.message));
    m_table->setItem(row, ColAcked, make(ev.acked ? QStringLiteral("已确认") : QStringLiteral("未确认")));
}

void AlarmPanel::slotAckSelected()
{
    const QList<QTableWidgetItem *> items = m_table->selectedItems();
    QSet<int> rows;
    for (const QTableWidgetItem *it : items)
        rows.insert(it->row());

    int done = 0;
    for (int r : rows) {
        const QTableWidgetItem *idItem = m_table->item(r, ColId);
        if (!idItem)
            continue;
        if (m_store->setAcked(idItem->data(Qt::UserRole).toLongLong(), true))
            ++done;
    }

    if (done > 0) {
        emit sigAudit(QStringLiteral("ALARM_ACK"), QStringLiteral("确认 %1 条报警").arg(done));
        reloadHistory();
    }
}

void AlarmPanel::slotAckAll()
{
    if (!m_store->ackAll())
        return;
    emit sigAudit(QStringLiteral("ALARM_ACK"), QStringLiteral("全部确认"));
    reloadHistory();
}

void AlarmPanel::slotClear()
{
    if (!m_canPurge)
        return;
    if (QMessageBox::question(this, QStringLiteral("确认清空"),
                              QStringLiteral("确定清空全部报警历史吗？该操作不可撤销。"))
        != QMessageBox::Yes)
        return;

    if (!m_store->clear()) {
        QMessageBox::warning(this, QStringLiteral("清空失败"), m_store->lastError());
        return;
    }
    emit sigAudit(QStringLiteral("ALARM_CLEAR"), QStringLiteral("清空报警历史"));
    reloadHistory();
}
