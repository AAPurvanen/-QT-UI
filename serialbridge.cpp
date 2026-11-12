#include "serialbridge.h"
#include "systate.h"

#include <QSettings>

#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif

SerialBridge::SerialBridge(SysState *state, QObject *parent)
    : QObject(parent)
    , m_state(state)
    , m_handle(nullptr)
    , m_ok(0)
    , m_bad(0)
{
    m_pollTimer.setInterval(20);
    connect(&m_pollTimer, &QTimer::timeout, this, &SerialBridge::poll);
}

SerialBridge::~SerialBridge()
{
    disconnectPort();
}

// 注册表 SERIALCOMM 枚举所有逻辑串口，CH340/CP2102/FT232 等驱动注册的都会出现
QStringList SerialBridge::availablePorts()
{
    QStringList list;
#ifdef Q_OS_WIN
    QSettings reg(QStringLiteral(R"(HKEY_LOCAL_MACHINE\HARDWARE\DEVICEMAP\SERIALCOMM)"),
                  QSettings::NativeFormat);
    const QStringList keys = reg.allKeys();
    for (const QString &k : keys) {
        const QString v = reg.value(k).toString();
        if (!v.isEmpty())
            list << v;
    }
#endif
    list.sort();
    return list;
}

bool SerialBridge::connectPort(const QString &portName, int baud)
{
    disconnectPort();

#ifdef Q_OS_WIN
    // COM 编号 >9 时必须用 \\.\ 前缀，否则 CreateFile 按设备名解析失败
    const QString devPath = QStringLiteral(R"(\\.\)") + portName;
    HANDLE h = CreateFileW(reinterpret_cast<const wchar_t *>(devPath.utf16()),
                           GENERIC_READ | GENERIC_WRITE,
                           0,               // 独占
                           nullptr,
                           OPEN_EXISTING,
                           0,               // 非重叠 I/O
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        emit errorText(QStringLiteral("打开 %1 失败 (错误码 %2，可能被占用或不存在)")
                           .arg(portName).arg(GetLastError()));
        return false;
    }

    // 8-N-1，无流控
    DCB dcb;
    ZeroMemory(&dcb, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb)) {
        CloseHandle(h);
        emit errorText(QStringLiteral("GetCommState 失败"));
        return false;
    }
    dcb.BaudRate = DWORD(baud);
    dcb.ByteSize = 8;
    dcb.Parity   = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;   // 某些 USB 转串口需要 DTR
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    if (!SetCommState(h, &dcb)) {
        CloseHandle(h);
        emit errorText(QStringLiteral("设置波特率 %1 失败").arg(baud));
        return false;
    }

    // 非阻塞读：ReadFile 立即返回当前可读的字节，配合 20ms QTimer 轮询
    COMMTIMEOUTS to;
    ZeroMemory(&to, sizeof(to));
    to.ReadIntervalTimeout       = MAXDWORD;
    to.ReadTotalTimeoutMultiplier = MAXDWORD;
    to.ReadTotalTimeoutConstant  = 1;
    to.WriteTotalTimeoutMultiplier = 2;
    to.WriteTotalTimeoutConstant   = 50;    // 阻塞上限，防拔线时写卡死
    SetCommTimeouts(h, &to);

    PurgeComm(h, PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);

    m_handle = h;
    m_rx.clear();
    m_ok = 0;
    m_bad = 0;
    m_pollTimer.start();
    emit statsChanged(m_ok, m_bad);
    emit connectedChanged(true);
    return true;
#else
    Q_UNUSED(portName);
    Q_UNUSED(baud);
    emit errorText(QStringLiteral("当前平台不支持串口"));
    return false;
#endif
}

void SerialBridge::disconnectPort()
{
#ifdef Q_OS_WIN
    if (m_handle != nullptr) {
        m_pollTimer.stop();
        CloseHandle(static_cast<HANDLE>(m_handle));
        m_handle = nullptr;
        emit connectedChanged(false);
    }
#endif
}

void SerialBridge::sendModeCommand(int mode)
{
#ifdef Q_OS_WIN
    if (m_handle == nullptr)
        return;
    const QByteArray cmd = QStringLiteral("CMD:MODE:%1\r\n").arg(mode).toLatin1();
    DWORD written = 0;
    if (!WriteFile(m_handle, cmd.constData(), DWORD(cmd.size()), &written, nullptr)) {
        emit errorText(QStringLiteral("发送失败，设备可能已拔出 (错误码 %1)").arg(GetLastError()));
        disconnectPort();
    }
#endif
}

// 20ms 轮询：读出的字节进缓冲，按 \n 分帧
void SerialBridge::poll()
{
#ifdef Q_OS_WIN
    if (m_handle == nullptr)
        return;

    char buf[256];
    DWORD n = 0;
    for (;;) {
        if (!ReadFile(m_handle, buf, sizeof(buf), &n, nullptr)) {
            emit errorText(QStringLiteral("串口读取失败，已断开 (错误码 %1)").arg(GetLastError()));
            disconnectPort();      // 设备拔出等致命错误
            return;
        }
        if (n == 0)
            break;
        m_rx.append(buf, int(n));
        if (m_rx.size() > 1024)    // 无换行的垃圾流保护
            m_rx.clear();
    }

    int idx;
    while ((idx = m_rx.indexOf('\n')) >= 0) {
        const QByteArray line = m_rx.left(idx).trimmed();
        m_rx.remove(0, idx + 1);
        processLine(line);
    }
#endif
}

// 解析 DIST:142,HUMAN:0,MODE:0
void SerialBridge::processLine(const QByteArray &line)
{
    if (line.isEmpty())
        return;

    const QString text = QString::fromLatin1(line);
    emit lineReceived(text);

    double dist = -1.0;
    int human = -1;
    int mode = -1;
    bool anyField = false;

    const QStringList fields = text.split(',');
    for (const QString &f : fields) {
        const QStringList kv = f.split(':');
        if (kv.size() != 2)
            continue;
        const QString k = kv[0].trimmed().toUpper();
        const QString v = kv[1].trimmed();
        bool ok = false;

        if (k == QLatin1String("DIST")) {
            dist = v.toDouble(&ok);
            anyField = true;
            if (!ok)
                dist = -1.0;
        } else if (k == QLatin1String("HUMAN")) {
            human = v.toInt(&ok);
            anyField = true;
            if (!ok)
                human = -1;
        } else if (k == QLatin1String("MODE")) {
            mode = v.toInt(&ok);
        }
    }

    // 至少含 DIST 与 HUMAN 且数值合法才算有效帧
    if (dist >= 0.0 && human >= 0 && anyField) {
        m_state->setDistance(dist);
        m_state->setHuman(human != 0);
        if (mode >= 0 && mode <= 2)
            m_state->setMode(static_cast<WorkMode>(mode));
        m_ok++;
    } else {
        m_bad++;
    }
    emit statsChanged(m_ok, m_bad);
}
