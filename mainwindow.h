#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

class SysState;
class DistanceWidget;
class SerialBridge;
class RecordStore;

class QLabel;
class QSlider;
class QPushButton;
class QTableWidget;
class QSpinBox;
class QGroupBox;
class QHBoxLayout;
class QComboBox;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onStateChanged();
    void onRecordsChanged();
    void onSliderDistanceChanged(int value);
    void onButtonModeMonitor();
    void onButtonModeSilent();
    void onButtonModeRecord();
    void onToggleHuman(bool checked);
    void onThresholdsChanged();

    // 串口
    void onPortsRefresh();
    void onConnectClicked();
    void onSerialConnected(bool connected);
    void onSerialText(const QString &text);
    void onSerialStats(int ok, int bad);

private:
    void setupUI();
    void setupConnections();
    QWidget* buildSerialBar();
    QWidget* buildStatusPanel();
    QWidget* buildControlPanel();
    QWidget* buildRecordPanel();
    void updateSimControls(bool serialActive);
    void updateSerialStatus();

    // 状态模型
    SysState *m_state;
    SerialBridge *m_serial;
    RecordStore *m_store;      // SQLite 持久化层

    // 中央控件
    DistanceWidget *m_gauge;

    // 左侧状态面板
    QLabel *m_modeLabel;
    QLabel *m_modeIcon;
    QLabel *m_humanLabel;
    QLabel *m_ledRed;
    QLabel *m_ledYellow;
    QLabel *m_ledGreen;
    QLabel *m_beepIcon;
    QLabel *m_beepLabel;

    // 右侧控制面板
    QSlider *m_sliderDistance;
    QLabel  *m_labelDistanceValue;
    QPushButton *m_btnHumanToggle;

    QPushButton *m_btnModeMonitor;
    QPushButton *m_btnModeSilent;
    QPushButton *m_btnModeRecord;

    QSpinBox *m_spinSafe;
    QSpinBox *m_spinWarn;
    QSpinBox *m_spinForce;

    // 记录列表
    QTableWidget *m_recordTable;

    // 串口栏控件
    QComboBox   *m_portCombo;
    QComboBox   *m_baudCombo;
    QPushButton *m_btnRefresh;
    QPushButton *m_btnConnect;
    QLabel      *m_serialStatus;
    QString      m_lastSerialLine;
    int          m_okFrames;
    int          m_badFrames;

    // 记录面板提示（显示数据库持久化总数）
    QLabel *m_recordHint;

    // 上一帧的状态快照：只在真正变化时更新界面，避免 50ms 重复计算
    int  m_lastMode;
    int  m_lastLevel;
    bool m_lastHuman;
    bool m_lastBeep;
};

#endif // MAINWINDOW_H
