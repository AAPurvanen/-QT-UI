#include "recordstore.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QDir>
#include <QStandardPaths>
#include <QVariant>
#include <QDebug>

RecordStore::RecordStore(QObject *parent)
    : QObject(parent)
    , m_ok(false)
{
    // 库文件放在用户数据目录，跟随程序而不是工作目录
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!QDir().mkpath(dir)) {
        qWarning() << "RecordStore: 无法创建数据目录" << dir;
        return;
    }

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"));
    m_db.setDatabaseName(dir + QStringLiteral("/alarm_history.db"));

    if (!m_db.open()) {
        qWarning() << "RecordStore: 打开数据库失败" << m_db.lastError().text();
        return;
    }

    QSqlQuery q;
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS alarm_records ("
            " id   INTEGER PRIMARY KEY AUTOINCREMENT,"
            " dist INTEGER NOT NULL,"
            " ts   TEXT    NOT NULL)"))) {
        qWarning() << "RecordStore: 建表失败" << q.lastError().text();
        return;
    }
    m_ok = true;
}

QList<AlarmRecord> RecordStore::loadLatest(int n)
{
    QList<AlarmRecord> out;
    if (!m_ok)
        return out;

    QSqlQuery q;
    // 新记录在前取 n 条，再反转成时间正序，与 SysState 环形缓冲的排列一致
    q.prepare(QStringLiteral(
        "SELECT dist, ts FROM alarm_records ORDER BY id DESC LIMIT :n"));
    q.bindValue(QStringLiteral(":n"), n);

    if (q.exec()) {
        while (q.next()) {
            const AlarmRecord rec{ q.value(0).toInt(),
                                   QDateTime::fromString(q.value(1).toString(), Qt::ISODate) };
            out.prepend(rec);
        }
    } else {
        qWarning() << "RecordStore: 查询失败" << q.lastError().text();
    }
    return out;
}

void RecordStore::append(int dist, const QDateTime &ts)
{
    if (!m_ok)
        return;

    QSqlQuery q;
    q.prepare(QStringLiteral(
        "INSERT INTO alarm_records (dist, ts) VALUES (:dist, :ts)"));
    q.bindValue(QStringLiteral(":dist"), dist);
    q.bindValue(QStringLiteral(":ts"), ts.toString(Qt::ISODate));

    if (!q.exec())
        qWarning() << "RecordStore: 写入失败" << q.lastError().text();
}

int RecordStore::totalCount()
{
    if (!m_ok)
        return 0;

    QSqlQuery q;
    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM alarm_records")) && q.next())
        return q.value(0).toInt();
    return 0;
}
