#ifndef SYSTATE_H
#define SYSTATE_H

#include <QObject>
#include <QTimer>
#include <QList>
#include <QDateTime>

// 工作模式 —— 与 config.h 完全对应
enum WorkMode {
    MODE_MONITOR = 0,  // 监控模式（声光全开）
    MODE_SILENT  = 1,  // 静默模式（仅亮灯）
    MODE_RECORD  = 2   // 记录查看
};

// 报警等级 —— 与 app_alarm.c 逻辑完全对应
enum AlarmLevel {
    LEVEL_SAFE    = 0,  // 安全（绿）
    LEVEL_WARN    = 1,  // 注意（黄）
    LEVEL_ALARM   = 2,  // 普通报警（红+蜂鸣慢闪）
    LEVEL_FORCE   = 3,  // 强制报警（红+蜂鸣快闪）
    LEVEL_SENSOR_RESET = 4  // 传感器复位中
};

// 报警记录 —— 与 config.h 中的 AlarmRecord 结构对应
struct AlarmRecord {
    int dist;           // 报警距离
    QDateTime timestamp; // 报警时间

    QString formatTime() const {
        return timestamp.toString("HH:mm:ss");
    }
};

// 阈值常量 —— 与 config.h 一致
static const int DEFAULT_SAFE_DIST  = 100;   // 安全距离 >100cm
static const int DEFAULT_WARN_DIST  = 30;    // 危险距离 <30cm
static const int DEFAULT_FORCE_DIST = 50;    // 有人+距离<50cm 强告警
static const int RECORD_MAX = 10;

class SysState : public QObject
{
    Q_OBJECT

public:
    explicit SysState(QObject *parent = nullptr);

    // ====== 状态读取接口 ======
    double distance()      const { return m_distance; }
    bool   humanDetected() const { return m_human; }
    WorkMode mode()        const { return m_mode; }
    AlarmLevel alarmLevel()const { return m_level; }
    bool   beepOn()        const { return m_beepOn; }
    const QList<AlarmRecord>& records() const { return m_records; }

    int safeDist()  const { return m_safeDist; }
    int warnDist()  const { return m_warnDist; }
    int forceDist() const { return m_forceDist; }

    // ====== 外部控制接口（模拟硬件输入） ======
    void setDistance(double cm);
    void setHuman(bool present);
    void setMode(WorkMode m);
    void cycleMode();
    void setThresholds(int safe, int warn, int force);

    // 程序启动时从持久化层恢复最近的历史记录（不超过 RECORD_MAX）
    void seedRecords(const QList<AlarmRecord> &records);

signals:
    void stateChanged();
    void recordsChanged();   // 仅在新增报警记录时发出，避免表格每帧重建
    void recordAdded(int dist, const QDateTime &ts);   // 供持久化层落盘

private slots:
    void onTick();
    void onBeepTick();

private:
    void evaluate();
    void addRecord(int dist);

    // 模拟硬件状态
    double m_distance;
    bool   m_human;
    WorkMode m_mode;

    // 阈值
    int m_safeDist;
    int m_warnDist;
    int m_forceDist;

    // 报警输出状态
    AlarmLevel m_level;
    bool   m_beepOn;

    // 报警记录：按时间先后排列，超过 RECORD_MAX 丢弃最旧的一条
    QList<AlarmRecord> m_records;

    // 定时器
    QTimer m_mainTimer;   // 50ms 主循环
    QTimer m_beepTimer;   // 蜂鸣节奏

    // 蜂鸣节拍计数
    int m_beepCounter;
};

#endif // SYSTATE_H
