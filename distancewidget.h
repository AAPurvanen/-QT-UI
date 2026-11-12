#ifndef DISTANCEWIDGET_H
#define DISTANCEWIDGET_H

#include <QWidget>
#include <QPainterPath>

// 简约圆形仪表盘控件
// 0~400cm 量程，根据距离自动切换颜色
class DistanceWidget : public QWidget
{
    Q_OBJECT

public:
    explicit DistanceWidget(QWidget *parent = nullptr);

    void setDistance(double cm);
    void setAlarmLevel(int level);  // 0=SAFE 1=WARN 2=ALARM 3=FORCE 4=RESET

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;

private:
    double m_distance;
    int    m_level;   // AlarmLevel

    QColor getArcColor() const;
    QColor getBackgroundColor() const;
};

#endif // DISTANCEWIDGET_H
