#include "systate.h"

namespace {
// 是否处于“需要记录/需要蜂鸣”的报警等级
bool isAlarmLevel(AlarmLevel l)
{
    return l == LEVEL_ALARM || l == LEVEL_FORCE;
}
}

SysState::SysState(QObject *parent)
    : QObject(parent)
    , m_distance(150.0)        // 初始安全距离
    , m_human(false)
    , m_mode(MODE_MONITOR)
    , m_safeDist(DEFAULT_SAFE_DIST)
    , m_warnDist(DEFAULT_WARN_DIST)
    , m_forceDist(DEFAULT_FORCE_DIST)
    , m_level(LEVEL_SAFE)
    , m_beepOn(false)
    , m_beepCounter(0)
{
    // 复刻单片机主循环 50ms 节拍
    m_mainTimer.setInterval(50);
    connect(&m_mainTimer, &QTimer::timeout, this, &SysState::onTick);
    m_mainTimer.start();

    // 蜂鸣节奏定时器（200ms 一拍）
    m_beepTimer.setInterval(200);
    connect(&m_beepTimer, &QTimer::timeout, this, &SysState::onBeepTick);
    m_beepTimer.start();
}

// ==================== 外部控制接口 ====================

void SysState::setDistance(double cm)
{
    m_distance = qBound(0.0, cm, 500.0);  // 合理范围 0~500cm
    evaluate();
}

void SysState::setHuman(bool present)
{
    m_human = present;
    evaluate();
}

void SysState::setMode(WorkMode m)
{
    m_mode = m;
    evaluate();
}

void SysState::cycleMode()
{
    int next = (int(m_mode) + 1) % 3;
    setMode(WorkMode(next));
}

void SysState::setThresholds(int safe, int warn, int force)
{
    m_safeDist  = qBound(20, safe, 300);
    m_warnDist  = qBound(10, warn, m_safeDist - 5);
    m_forceDist = qBound(m_warnDist, force, m_safeDist);
    evaluate();
}

// ==================== 主循环节拍 ====================

void SysState::onTick()
{
    // 每 50ms 评估一次（与单片机 Delay_ms(50) 周期一致）
    evaluate();
}

// ==================== 报警逻辑（复刻 app_alarm.c Alarm_Check）====================

void SysState::seedRecords(const QList<AlarmRecord> &records)
{
    // 只保留最近 RECORD_MAX 条，时间正序（与 addRecord 的排列语义一致）
    m_records = records;

    while (m_records.size() > RECORD_MAX)
        m_records.removeFirst();
}

void SysState::evaluate()
{
    const int dist = int(m_distance);
    AlarmLevel newLevel;

    if (dist >= 800) {
        newLevel = LEVEL_SENSOR_RESET;
    } else if (m_human && dist < m_forceDist) {
        newLevel = LEVEL_FORCE;
    } else if (dist < m_warnDist) {
        newLevel = LEVEL_ALARM;
    } else if (dist < m_safeDist) {
        newLevel = LEVEL_WARN;
    } else {
        newLevel = LEVEL_SAFE;
    }

    // 只在“非报警 → 报警”的上升沿记录一次。
    // 原固件每 50ms 调一次 Alarm_RecordSave，会在 0.5 秒内把 10 条记录全填满，
    // 这里改为状态跳变触发，语义才是“一次报警事件一条记录”。
    if (isAlarmLevel(newLevel) && !isAlarmLevel(m_level)) {
        addRecord(dist);
    }

    m_level = newLevel;

    // 静默/记录模式或非报警状态下不蜂鸣
    if (!isAlarmLevel(newLevel) || m_mode != MODE_MONITOR) {
        m_beepOn = false;
    }

    emit stateChanged();
}

/*************************************************
 * 函数: addRecord
 * 功能: 追加一条报警记录，超过上限时丢弃最旧的一条
 *************************************************/
void SysState::addRecord(int dist)
{
    if (m_records.size() >= RECORD_MAX)
        m_records.removeFirst();

    AlarmRecord rec{dist, QDateTime::currentDateTime()};
    m_records.append(rec);
    emit recordsChanged();
    emit recordAdded(dist, rec.timestamp);
}

// ==================== 蜂鸣节奏控制 ====================
// 复刻单片机 BEEP_AlarmMode 的节拍逻辑（每 200ms 一拍）
//   强制报警：通断交替   → 周期 400ms
//   普通报警：响 3 停 1   → 周期 1.2s
void SysState::onBeepTick()
{
    if (m_mode != MODE_MONITOR || !isAlarmLevel(m_level)) {
        m_beepOn = false;
        return;
    }

    m_beepCounter++;

    if (m_level == LEVEL_FORCE)
        m_beepOn = (m_beepCounter % 2 == 0);   // 快闪 400ms
    else
        m_beepOn = (m_beepCounter % 4 != 0);   // 慢闪 1.2s（第 4 拍停）
}
