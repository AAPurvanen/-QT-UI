#ifndef SERIALBRIDGE_H
#define SERIALBRIDGE_H

#include <QObject>
#include <QTimer>
#include <QByteArray>
#include <QStringList>

class SysState;

/*
 * 串口桥 —— 用 Win32 API 实现，不依赖 QtSerialPort 模块（该模块未安装）。
 * QSerialPort 底层同样是 CreateFile/ReadFile 这套 Windows API，行为一致。
 *
 * 通信协议（纯 ASCII，每帧以 \r\n 或 \n 结尾）：
 *   单片机 → 上位机（每 500ms 一帧）:
 *       DIST:142,HUMAN:0,MODE:0
 *   上位机 → 单片机（界面切模式时发送）:
 *       CMD:MODE:1
 */
class SerialBridge : public QObject
{
    Q_OBJECT

public:
    explicit SerialBridge(SysState *state, QObject *parent = nullptr);
    ~SerialBridge() override;

    // 枚举本机可用串口（含 CH340/CP2102 等 USB 转串口）
    static QStringList availablePorts();

    bool isConnected() const { return m_handle != nullptr; }

public slots:
    bool connectPort(const QString &portName, int baud);
    void disconnectPort();
    void sendModeCommand(int mode);

signals:
    void connectedChanged(bool connected);
    void lineReceived(const QString &line);   // 原始行（含错误提示），用于状态栏显示
    void statsChanged(int okFrames, int badFrames);
    void errorText(const QString &text);

private slots:
    void poll();

private:
    void processLine(const QByteArray &line);

    SysState *m_state;
    void     *m_handle;      // HANDLE。用 void* 避免 windows.h 泄漏进头文件
    QTimer    m_pollTimer;   // 20ms 轮询读串口
    QByteArray m_rx;
    int m_ok;
    int m_bad;
};

#endif // SERIALBRIDGE_H
