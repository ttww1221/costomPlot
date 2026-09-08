/**
 * @file    WavePanel.cpp
 * @brief   波形面板占位实现（S3 接入 QCustomPlot 后重写）
 */

#include "WavePanel.h"
#include "qcustomplot.h"
#include <QLabel>
#include <QVBoxLayout>

WavePanel::WavePanel(QWidget *parent)
    : QWidget(parent)
{
    m_lblPlaceholder = new QLabel(
        QStringLiteral("实时波形显示区\n(S3 阶段接入 QCustomPlot：温度 / 湿度 / 设定值 / 仪表角度)"), this);
    m_lblPlaceholder->setAlignment(Qt::AlignCenter);
    m_lblPlaceholder->setStyleSheet(QStringLiteral(
        "color: #7f8c8d; background-color: #ecf0f1; border: 1px dashed #bdc3c7; border-radius: 6px;"));

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_lblPlaceholder);
}
