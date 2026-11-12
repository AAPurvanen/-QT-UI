#include "distancewidget.h"
#include <QPainter>
#include <QtMath>

// 量程 0~400cm（与 HC-SR04 规格一致）
static const double MAX_DIST = 400.0;

// 统一几何约定：
//   起点 225°（左下角，0cm），顺时针扫 270°，终点 -45°（右下角，400cm）
//   Qt 的 drawArc 角度与下面刻度用的数学角度是同一套约定：
//   点坐标 = (cx + r*cosθ, cy - r*sinθ)，θ 为角度制
static const double START_ANGLE = 225.0;
static const double SWEEP_ANGLE = -270.0;   // 负值 = 顺时针

DistanceWidget::DistanceWidget(QWidget *parent)
    : QWidget(parent)
    , m_distance(0.0)
    , m_level(0)
{
    setMinimumSize(240, 240);
}

void DistanceWidget::setDistance(double cm)
{
    if (m_distance == cm)
        return;
    m_distance = cm;
    update();
}

void DistanceWidget::setAlarmLevel(int level)
{
    if (m_level == level)
        return;
    m_level = level;
    update();
}

QSize DistanceWidget::sizeHint() const
{
    return QSize(280, 280);
}

QSize DistanceWidget::minimumSizeHint() const
{
    return QSize(240, 240);
}

QColor DistanceWidget::getArcColor() const
{
    switch (m_level) {
    case 0: return QColor(76, 175, 80);     // SAFE  绿
    case 1: return QColor(255, 152, 0);     // WARN  橙
    case 2: return QColor(244, 67, 54);     // ALARM 红
    case 3: return QColor(211, 47, 47);     // FORCE 深红
    case 4: return QColor(158, 158, 158);   // RESET 灰
    default: return QColor(76, 175, 80);
    }
}

QColor DistanceWidget::getBackgroundColor() const
{
    // 简约白色风：根据级别给很淡的背景色
    switch (m_level) {
    case 0: return QColor(232, 245, 233);   // 淡绿
    case 1: return QColor(255, 243, 224);   // 淡橙
    case 2: return QColor(255, 235, 238);   // 淡红
    case 3: return QColor(255, 224, 224);   // 深红底
    case 4: return QColor(245, 245, 245);   // 淡灰
    default: return QColor(250, 250, 250);
    }
}

void DistanceWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const double w = width();
    const double h = height();
    const double side = qMin(w, h);
    const double cx = w / 2.0;
    const double cy = h / 2.0;

    // === 背景卡片 ===
    p.setPen(QPen(QColor(228, 230, 239), 1));
    p.setBrush(getBackgroundColor());
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);

    const double arcR = side / 2.0 - 26.0;
    const QRectF arcRect(cx - arcR, cy - arcR, arcR * 2, arcR * 2);

    // === 背景轨道（完整 270°） ===
    QPen trackPen(QColor(232, 234, 240));
    trackPen.setWidth(12);
    trackPen.setCapStyle(Qt::RoundCap);
    p.setPen(trackPen);
    p.drawArc(arcRect, qRound(START_ANGLE * 16), qRound(SWEEP_ANGLE * 16));

    // === 进度弧（从 0cm 起，按比例顺时针增长） ===
    const double ratio = qBound(0.0, m_distance / MAX_DIST, 1.0);
    if (ratio > 0.0) {
        QPen progPen(getArcColor());
        progPen.setWidth(12);
        progPen.setCapStyle(Qt::RoundCap);
        p.setPen(progPen);
        p.drawArc(arcRect, qRound(START_ANGLE * 16),
                  qRound(SWEEP_ANGLE * ratio * 16));
    }

    // === 刻度 + 刻度值（与弧线同一套角度公式） ===
    const int tickCount = 8;   // 8 段 → 0/50/.../400 共 9 个刻度
    QFont tickFont = font();
    tickFont.setPointSize(8);

    for (int i = 0; i <= tickCount; ++i) {
        const double t = double(i) / tickCount;
        const double theta = qDegreesToRadians(START_ANGLE + SWEEP_ANGLE * t);
        const double cs = qCos(theta);
        const double sn = qSin(theta);

        // 刻度短线
        p.setPen(QPen(QColor(185, 188, 196), 1));
        p.drawLine(QPointF(cx + cs * (arcR - 8),  cy - sn * (arcR - 8)),
                   QPointF(cx + cs * (arcR - 15), cy - sn * (arcR - 15)));

        // 刻度数字
        p.setFont(tickFont);
        p.setPen(QColor(125, 130, 140));
        const QPointF lp(cx + cs * (arcR - 30), cy - sn * (arcR - 30));
        p.drawText(QRectF(lp.x() - 20, lp.y() - 9, 40, 18),
                   Qt::AlignCenter, QString::number(int(MAX_DIST * t)));
    }

    // === 中心距离数值 ===
    QFont valueFont = font();
    valueFont.setPointSize(30);
    valueFont.setBold(true);
    p.setFont(valueFont);
    p.setPen(getArcColor());
    p.drawText(QRectF(cx - 90, cy - 40, 180, 46), Qt::AlignCenter,
               QString::number(int(m_distance)));

    // === 单位 ===
    QFont unitFont = font();
    unitFont.setPointSize(11);
    p.setFont(unitFont);
    p.setPen(QColor(120, 125, 135));
    p.drawText(QRectF(cx - 40, cy + 6, 80, 20), Qt::AlignCenter, "cm");

    // === 状态文字（放在弧线底部缺口内） ===
    QString statusText;
    QColor statusColor;
    switch (m_level) {
    case 0: statusText = "安全";       statusColor = QColor(76, 175, 80);   break;
    case 1: statusText = "注意";       statusColor = QColor(255, 152, 0);   break;
    case 2: statusText = "报警";       statusColor = QColor(244, 67, 54);   break;
    case 3: statusText = "强制报警";   statusColor = QColor(211, 47, 47);   break;
    case 4: statusText = "传感器复位"; statusColor = QColor(158, 158, 158); break;
    }

    QFont statusFont = font();
    statusFont.setPointSize(12);
    statusFont.setBold(true);
    p.setFont(statusFont);
    p.setPen(statusColor);
    p.drawText(QRectF(cx - 50, cy + 38, 100, 26), Qt::AlignCenter, statusText);
}
