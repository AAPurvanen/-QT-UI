#include <QtTest>
#include "systate.h"

/*
 * SysState 单元测试 —— 只测业务逻辑层，不碰任何界面代码。
 *
 * 说明：SysState 内部的 QTimer 只有在事件循环里才会触发，
 * QTest 的同步断言不依赖事件循环，因此这里的行为是确定的：
 *   setDistance / setHuman / setThresholds 调用瞬间同步触发 evaluate()。
 * 蜂鸣节拍（onBeepTick）依赖 200ms 定时器，属于节拍层，不在此测试。
 */
class SysStateTest : public QObject
{
    Q_OBJECT

private slots:
    void safeZone();              // >= safe → SAFE，无记录
    void warnZoneBoundaries();    // warn <= d < safe → WARN
    void alarmZoneBoundaries();   // d < warn → ALARM，且只记 1 条
    void risingEdgeOnlyRecords(); // 持续报警不重复记录，离开再进入才记下一条
    void forceAlarmWithHuman();   // 有人 + d < force → FORCE（优先于 WARN/ALARM）
    void forceNeedsHumanOnly();   // 没有人时同样距离不触发 FORCE
    void silentModeNoBeep();      // 静默模式不蜂鸣
    void recordModeNoBeep();      // 记录模式不蜂鸣
    void thresholdClamping();     // 阈值自动钳位：warn < force <= safe
    void ringBufferKeeps10();     // 环形缓冲最多 10 条，超出丢最旧
    void seedRecordsRestores();   // 启动时从持久化层恢复历史

private:
    // 构造一个已稳定在安全区（150cm）的干净状态机
    SysState* makeSafeState()
    {
        return new SysState(this);   // 初始 150cm = SAFE
    }
};

void SysStateTest::safeZone()
{
    QScopedPointer<SysState> s(makeSafeState());
    QCOMPARE(s->alarmLevel(), LEVEL_SAFE);
    QCOMPARE(s->records().size(), 0);
    QCOMPARE(s->beepOn(), false);

    s->setDistance(400.0);
    QCOMPARE(s->alarmLevel(), LEVEL_SAFE);
    QCOMPARE(s->records().size(), 0);
}

void SysStateTest::warnZoneBoundaries()
{
    QScopedPointer<SysState> s(makeSafeState());
    // 默认阈值: safe=100, warn=30
    s->setDistance(100.0);                       // 100 = safe 边界，仍是安全
    QCOMPARE(s->alarmLevel(), LEVEL_SAFE);

    s->setDistance(99.0);                        // 100 > d >= 30 → WARN
    QCOMPARE(s->alarmLevel(), LEVEL_WARN);

    s->setDistance(30.0);                        // 30 = warn 边界，仍是 WARN
    QCOMPARE(s->alarmLevel(), LEVEL_WARN);

    QCOMPARE(s->records().size(), 0);            // WARN 不产生记录
}

void SysStateTest::alarmZoneBoundaries()
{
    QScopedPointer<SysState> s(makeSafeState());
    s->setDistance(29.0);                        // 30 > d → ALARM
    QCOMPARE(s->alarmLevel(), LEVEL_ALARM);
    QCOMPARE(s->records().size(), 1);
    QCOMPARE(s->records().last().dist, 29);      // 记录触发时刻的距离

    s->setDistance(0.0);                         // 持续深入报警区
    QCOMPARE(s->alarmLevel(), LEVEL_ALARM);
}

void SysStateTest::risingEdgeOnlyRecords()
{
    QScopedPointer<SysState> s(makeSafeState());

    s->setDistance(20.0);                        // 进报警区 → 记第 1 条
    QCOMPARE(s->records().size(), 1);

    for (int i = 0; i < 100; ++i)                // 持续报警 100 拍不再刷记录
        s->setDistance(20.0 + i * 0.1);          // 20~30，仍在报警区
    QCOMPARE(s->records().size(), 1);

    s->setDistance(150.0);                       // 回安全区
    QCOMPARE(s->alarmLevel(), LEVEL_SAFE);

    s->setDistance(25.0);                        // 再次进入 → 第 2 条
    QCOMPARE(s->alarmLevel(), LEVEL_ALARM);
    QCOMPARE(s->records().size(), 2);
}

void SysStateTest::forceAlarmWithHuman()
{
    QScopedPointer<SysState> s(makeSafeState());
    s->setHuman(true);
    s->setDistance(45.0);                        // 45 < force(50) → FORCE
    QCOMPARE(s->alarmLevel(), LEVEL_FORCE);
    QCOMPARE(s->records().size(), 1);            // 强制报警同样走上升沿记录
}

void SysStateTest::forceNeedsHumanOnly()
{
    QScopedPointer<SysState> s(makeSafeState());
    s->setDistance(45.0);                        // 无人: 45 >= warn(30) → 只是 WARN
    QCOMPARE(s->alarmLevel(), LEVEL_WARN);
    QCOMPARE(s->records().size(), 0);
}

void SysStateTest::silentModeNoBeep()
{
    QScopedPointer<SysState> s(makeSafeState());
    s->setMode(MODE_SILENT);
    s->setDistance(20.0);
    QCOMPARE(s->alarmLevel(), LEVEL_ALARM);
    QCOMPARE(s->beepOn(), false);                // 静默模式：灯亮不响
}

void SysStateTest::recordModeNoBeep()
{
    QScopedPointer<SysState> s(makeSafeState());
    s->setMode(MODE_RECORD);
    s->setDistance(20.0);
    QCOMPARE(s->alarmLevel(), LEVEL_ALARM);
    QCOMPARE(s->beepOn(), false);
}

void SysStateTest::thresholdClamping()
{
    QScopedPointer<SysState> s(makeSafeState());

    // 用户乱填：safe=100, warn=90, force=95
    // 钳位规则: safe∈[20,300], warn∈[10,safe-5], force∈[warn,safe]
    s->setThresholds(100, 90, 95);
    QCOMPARE(s->safeDist(), 100);
    QCOMPARE(s->warnDist(), 90);
    QCOMPARE(s->forceDist(), 95);

    // 更离谱的输入: safe=60, warn=200, force=10
    // 不变量: warn <= force <= safe，且 warn <= safe-5
    s->setThresholds(60, 200, 10);
    QVERIFY(s->warnDist() <= s->forceDist());
    QVERIFY(s->forceDist() <= s->safeDist());
    QVERIFY(s->warnDist() <= s->safeDist() - 5);

    // 行为一致性：钳位后三级边界仍正确
    QScopedPointer<SysState> s2(makeSafeState());
    s2->setThresholds(80, 40, 60);
    s2->setDistance(60.0);                       // 40 <= 60 < 80 → WARN
    QCOMPARE(s2->alarmLevel(), LEVEL_WARN);
    s2->setDistance(39.0);                       // 39 < 40 → ALARM
    QCOMPARE(s2->alarmLevel(), LEVEL_ALARM);
    s2->setHuman(true);
    s2->setDistance(55.0);                       // 有人 && 55 < force(60) → FORCE
    QCOMPARE(s2->alarmLevel(), LEVEL_FORCE);
}

void SysStateTest::ringBufferKeeps10()
{
    QScopedPointer<SysState> s(makeSafeState());

    // 反复进出报警区 15 次，缓冲应保持在 10 条且最旧的被丢弃
    for (int i = 0; i < 15; ++i) {
        s->setDistance(150.0);                   // 回安全
        s->setDistance(20.0);                    // 进报警（记 1 条）
        QCOMPARE(s->alarmLevel(), LEVEL_ALARM);
    }

    QCOMPARE(s->records().size(), RECORD_MAX);
    // 最后一条应是最后一次触发的距离 20
    QCOMPARE(s->records().last().dist, 20);
}

void SysStateTest::seedRecordsRestores()
{
    QScopedPointer<SysState> s(makeSafeState());

    // 模拟启动时从 SQLite 读回的历史（时间正序）
    QList<AlarmRecord> history;
    const QDateTime base = QDateTime::currentDateTime().addSecs(-600);
    for (int i = 0; i < 12; ++i)                 // 库里 12 条 > RECORD_MAX
        history.append(AlarmRecord{100 + i, base.addSecs(i * 60)});

    s->seedRecords(history);
    QCOMPARE(s->records().size(), RECORD_MAX);   // 只保留最近 10 条
    // 时间正序：第 0 条应是第 3 次事件（最旧的被丢掉）
    QCOMPARE(s->records().first().dist, 102);
    QCOMPARE(s->records().last().dist, 111);
    QCOMPARE(s->records().last().formatTime(),
             base.addSecs(11 * 60).toString("HH:mm:ss"));
}

QTEST_MAIN(SysStateTest)
#include "systate_test.moc"
