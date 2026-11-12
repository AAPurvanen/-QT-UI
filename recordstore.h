#ifndef RECORDSTORE_H
#define RECORDSTORE_H

#include <QObject>
#include <QList>
#include <QDateTime>
#include <QSqlDatabase>

#include "systate.h"

/*
 * RecordStore —— 报警记录的 SQLite 持久化层。
 *
 * 职责划分：
 *   SysState      内存中最近 10 条（与单片机环形缓冲语义一致，用于界面展示）
 *   RecordStore   全量历史落盘（跨程序运行保留，用于持久化与追溯）
 *
 * 数据流：
 *   SysState::addRecord ──► recordAdded(dist, ts) ──► RecordStore::append
 *   程序启动时：loadLatest(10) ──► SysState::seedRecords（跨次运行的"记忆"）
 *
 * 表结构：
 *   CREATE TABLE alarm_records (
 *       id   INTEGER PRIMARY KEY AUTOINCREMENT,
 *       dist INTEGER NOT NULL,
 *       ts   TEXT    NOT NULL      -- ISO 8601 格式
 *   )
 */
class RecordStore : public QObject
{
    Q_OBJECT

public:
    explicit RecordStore(QObject *parent = nullptr);

    QList<AlarmRecord> loadLatest(int n);       // 取最近 n 条，按时间正序返回
    void append(int dist, const QDateTime &ts);
    int  totalCount();

private:
    QSqlDatabase m_db;
    bool m_ok;                                  // 数据库初始化是否成功
};

#endif // RECORDSTORE_H
