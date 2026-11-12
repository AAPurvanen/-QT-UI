#include "mainwindow.h"
#include "systate.h"
#include "distancewidget.h"
#include "serialbridge.h"
#include "recordstore.h"

#include <QWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSlider>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QComboBox>
#include <QHeaderView>
#include <QApplication>
#include <QResource>
#include <QFont>
#include <QIcon>
#include <QFile>
#include <QIODevice>
#include <QStyle>
#include <QScreen>

namespace {
// 动态属性变化后需要重新应用 QSS
void repolish(QWidget *w)
{
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_state(nullptr)
    , m_serial(nullptr)
    , m_lastSerialLine()
    , m_okFrames(0)
    , m_badFrames(0)
    , m_lastMode(-1)
    , m_lastLevel(-1)
    , m_lastHuman(false)
    , m_lastBeep(false)
{
    // 加载样式表
    QFile qssFile(":/styles.qss");
    if (qssFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString qss = QString::fromUtf8(qssFile.readAll());
        qApp->setStyleSheet(qss);
    }

    m_state = new SysState(this);
    m_serial = new SerialBridge(m_state, this);
    m_store = new RecordStore(this);

    // 启动时从 SQLite 恢复历史记录，界面立即有"记忆"
    m_state->seedRecords(m_store->loadLatest(RECORD_MAX));

    setupUI();
    setupConnections();

    // 每次报警 → 落盘 SQLite，并刷新持久化总数提示
    connect(m_state, &SysState::recordAdded, this, [this](int dist, const QDateTime &ts) {
        m_store->append(dist, ts);
        m_recordHint->setText(QStringLiteral("最多显示最近 10 条报警\nSQLite 已持久化 %1 条")
                                  .arg(m_store->totalCount()));
    });

    onStateChanged();     // 初始化显示
    onRecordsChanged();
    onPortsRefresh();     // 首次枚举串口

    // 显示历史持久化总数
    const int total = m_store->totalCount();
    if (total > 0)
        m_recordHint->setText(QStringLiteral("最多显示最近 10 条报警\nSQLite 已持久化 %1 条").arg(total));
}

MainWindow::~MainWindow()
{
}

// ==================== UI 构建 ====================

void MainWindow::setupUI()
{
    setWindowTitle(QStringLiteral("超声波距离监测与安全预警系统 — 上位机控制台"));

    // 按可用屏幕区域自适应并居中，避免高 DPI 下列被裁掉
    const QRect avail = screen()->availableGeometry();
    resize(qMin(980, avail.width() - 30), qMin(660, avail.height() - 30));
    move(avail.center() - rect().center());
    setMinimumSize(880, 540);

    QWidget *central = new QWidget(this);
    setCentralWidget(central);

    QVBoxLayout *outer = new QVBoxLayout(central);
    outer->setContentsMargins(16, 16, 16, 16);
    outer->setSpacing(12);

    // 顶部串口连接栏
    outer->addWidget(buildSerialBar(), 0);

    // 三栏比例 2 : 5 : 3
    QWidget *row = new QWidget();
    QHBoxLayout *mainLayout = new QHBoxLayout(row);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addWidget(buildStatusPanel(),  2);
    mainLayout->addWidget(buildControlPanel(), 5);
    mainLayout->addWidget(buildRecordPanel(),  3);
    outer->addWidget(row, 1);
}

// 顶部串口栏：端口 / 波特率 / 刷新 / 连接 / 状态
QWidget* MainWindow::buildSerialBar()
{
    QWidget *bar = new QWidget();
    bar->setObjectName("serialBar");

    QHBoxLayout *l = new QHBoxLayout(bar);
    l->setContentsMargins(10, 8, 10, 8);
    l->setSpacing(8);

    l->addWidget(new QLabel(QStringLiteral("串口")));

    m_portCombo = new QComboBox();
    m_portCombo->setMinimumWidth(100);
    l->addWidget(m_portCombo);

    m_baudCombo = new QComboBox();
    m_baudCombo->addItems({QStringLiteral("4800"), QStringLiteral("9600"),
                           QStringLiteral("19200"), QStringLiteral("115200")});
    m_baudCombo->setCurrentText(QStringLiteral("9600"));
    l->addWidget(m_baudCombo);

    m_btnRefresh = new QPushButton(QStringLiteral("刷新"));
    l->addWidget(m_btnRefresh);

    m_btnConnect = new QPushButton(QStringLiteral("连接"));
    l->addWidget(m_btnConnect);

    m_serialStatus = new QLabel(QStringLiteral("未连接"));
    m_serialStatus->setObjectName("serialStatus");
    l->addWidget(m_serialStatus, 1);   // 拉伸占满剩余宽度

    return bar;
}

// 左侧：状态面板
QWidget* MainWindow::buildStatusPanel()
{
    QWidget *panel = new QWidget();
    panel->setObjectName("statusPanel");

    QVBoxLayout *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(14);

    // --- 当前模式卡片 ---
    QGroupBox *modeBox = new QGroupBox(QStringLiteral("当前工作模式"));
    QVBoxLayout *modeLayout = new QVBoxLayout(modeBox);
    m_modeIcon = new QLabel(QStringLiteral("●"));
    m_modeIcon->setAlignment(Qt::AlignCenter);
    m_modeIcon->setObjectName("modeIcon");

    m_modeLabel = new QLabel(QStringLiteral("监控模式"));
    m_modeLabel->setAlignment(Qt::AlignCenter);
    m_modeLabel->setObjectName("modeLabel");

    modeLayout->addWidget(m_modeIcon);
    modeLayout->addWidget(m_modeLabel);
    layout->addWidget(modeBox);

    // --- 人体红外卡片 ---
    QGroupBox *humanBox = new QGroupBox(QStringLiteral("人体红外检测"));
    QVBoxLayout *humanLayout = new QVBoxLayout(humanBox);
    m_humanLabel = new QLabel(QStringLiteral("未检测到人体"));
    m_humanLabel->setAlignment(Qt::AlignCenter);
    m_humanLabel->setObjectName("humanLabel");
    humanLayout->addWidget(m_humanLabel);
    layout->addWidget(humanBox);

    // --- 三色 LED 卡片 ---
    QGroupBox *ledBox = new QGroupBox(QStringLiteral("三色指示灯"));
    QHBoxLayout *ledLayout = new QHBoxLayout(ledBox);
    ledLayout->setSpacing(12);

    // 红
    QVBoxLayout *redCol = new QVBoxLayout();
    m_ledRed = new QLabel("○");
    m_ledRed->setAlignment(Qt::AlignCenter);
    m_ledRed->setObjectName("ledRed");
    QLabel *redText = new QLabel(QStringLiteral("危险"));
    redText->setAlignment(Qt::AlignCenter);
    redText->setObjectName("ledText");
    redCol->addWidget(m_ledRed);
    redCol->addWidget(redText);

    // 黄
    QVBoxLayout *yellowCol = new QVBoxLayout();
    m_ledYellow = new QLabel("○");
    m_ledYellow->setAlignment(Qt::AlignCenter);
    m_ledYellow->setObjectName("ledYellow");
    QLabel *yellowText = new QLabel(QStringLiteral("注意"));
    yellowText->setAlignment(Qt::AlignCenter);
    yellowText->setObjectName("ledText");
    yellowCol->addWidget(m_ledYellow);
    yellowCol->addWidget(yellowText);

    // 绿
    QVBoxLayout *greenCol = new QVBoxLayout();
    m_ledGreen = new QLabel("○");
    m_ledGreen->setAlignment(Qt::AlignCenter);
    m_ledGreen->setObjectName("ledGreen");
    QLabel *greenText = new QLabel(QStringLiteral("安全"));
    greenText->setAlignment(Qt::AlignCenter);
    greenText->setObjectName("ledText");
    greenCol->addWidget(m_ledGreen);
    greenCol->addWidget(greenText);

    ledLayout->addLayout(redCol);
    ledLayout->addLayout(yellowCol);
    ledLayout->addLayout(greenCol);
    layout->addWidget(ledBox);

    // --- 蜂鸣器卡片 ---
    QGroupBox *beepBox = new QGroupBox(QStringLiteral("蜂鸣器状态"));
    QVBoxLayout *beepLayout = new QVBoxLayout(beepBox);
    m_beepIcon = new QLabel(QStringLiteral("🔇"));
    m_beepIcon->setAlignment(Qt::AlignCenter);
    m_beepIcon->setObjectName("beepIcon");
    m_beepLabel = new QLabel(QStringLiteral("关闭"));
    m_beepLabel->setAlignment(Qt::AlignCenter);
    m_beepLabel->setObjectName("beepLabel");
    beepLayout->addWidget(m_beepIcon);
    beepLayout->addWidget(m_beepLabel);
    layout->addWidget(beepBox);

    layout->addStretch(1);
    return panel;
}

// 中央 + 右侧：仪表盘 + 控制面板
QWidget* MainWindow::buildControlPanel()
{
    QWidget *panel = new QWidget();
    panel->setObjectName("centerPanel");

    QVBoxLayout *outer = new QVBoxLayout(panel);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(12);

    // --- 距离仪表盘 ---
    m_gauge = new DistanceWidget();
    m_gauge->setObjectName("gauge");
    outer->addWidget(m_gauge, 1);

    // --- 下方面板（模式切换 + 模拟输入 + 阈值设置） ---
    QWidget *bottom = new QWidget();
    QHBoxLayout *botLayout = new QHBoxLayout(bottom);
    botLayout->setContentsMargins(0, 0, 0, 0);
    botLayout->setSpacing(10);

    // 左下：模式切换
    QGroupBox *modeBox = new QGroupBox(QStringLiteral("模式切换"));
    QVBoxLayout *modeLayout = new QVBoxLayout(modeBox);
    m_btnModeMonitor = new QPushButton(QStringLiteral("监控模式"));
    m_btnModeSilent  = new QPushButton(QStringLiteral("静默模式"));
    m_btnModeRecord  = new QPushButton(QStringLiteral("记录查看"));
    m_btnModeMonitor->setObjectName("btnMode");
    m_btnModeSilent->setObjectName("btnMode");
    m_btnModeRecord->setObjectName("btnMode");
    modeLayout->addWidget(m_btnModeMonitor);
    modeLayout->addWidget(m_btnModeSilent);
    modeLayout->addWidget(m_btnModeRecord);
    botLayout->addWidget(modeBox, 1);

    // 中下：模拟输入
    QGroupBox *simBox = new QGroupBox(QStringLiteral("模拟硬件输入"));
    QVBoxLayout *simLayout = new QVBoxLayout(simBox);

    QLabel *sliderLabel = new QLabel(QStringLiteral("距离模拟 (cm)"));
    simLayout->addWidget(sliderLabel);

    m_sliderDistance = new QSlider(Qt::Horizontal);
    m_sliderDistance->setRange(0, 400);
    m_sliderDistance->setValue(150);
    simLayout->addWidget(m_sliderDistance);

    m_labelDistanceValue = new QLabel("150 cm");
    m_labelDistanceValue->setAlignment(Qt::AlignCenter);
    m_labelDistanceValue->setObjectName("distanceValueLabel");
    simLayout->addWidget(m_labelDistanceValue);

    m_btnHumanToggle = new QPushButton(QStringLiteral("人体检测：关"));
    m_btnHumanToggle->setCheckable(true);
    m_btnHumanToggle->setObjectName("humanToggle");
    simLayout->addWidget(m_btnHumanToggle);

    botLayout->addWidget(simBox, 1);

    // 右下：阈值设置
    QGroupBox *thBox = new QGroupBox(QStringLiteral("报警阈值 (cm)"));
    QGridLayout *thLayout = new QGridLayout(thBox);

    thLayout->addWidget(new QLabel(QStringLiteral("强制报警")), 0, 0);
    m_spinForce = new QSpinBox();
    m_spinForce->setRange(10, 200);
    m_spinForce->setValue(50);
    thLayout->addWidget(m_spinForce, 0, 1);

    thLayout->addWidget(new QLabel(QStringLiteral("普通报警")), 1, 0);
    m_spinWarn = new QSpinBox();
    m_spinWarn->setRange(5, 150);
    m_spinWarn->setValue(30);
    thLayout->addWidget(m_spinWarn, 1, 1);

    thLayout->addWidget(new QLabel(QStringLiteral("注意距离")), 2, 0);
    m_spinSafe = new QSpinBox();
    m_spinSafe->setRange(30, 300);
    m_spinSafe->setValue(100);
    thLayout->addWidget(m_spinSafe, 2, 1);

    botLayout->addWidget(thBox, 1);

    outer->addWidget(bottom, 0);
    return panel;
}

// 右侧：历史记录
QWidget* MainWindow::buildRecordPanel()
{
    QWidget *panel = new QWidget();
    panel->setObjectName("recordPanel");

    QVBoxLayout *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    QLabel *title = new QLabel(QStringLiteral("报警历史记录"));
    title->setObjectName("recordTitle");
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    m_recordTable = new QTableWidget(0, 3);
    m_recordTable->setHorizontalHeaderLabels({
        QStringLiteral("序号"),
        QStringLiteral("距离 (cm)"),
        QStringLiteral("时间")
    });
    m_recordTable->horizontalHeader()->setStretchLastSection(true);
    m_recordTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_recordTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_recordTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_recordTable->setAlternatingRowColors(true);
    m_recordTable->verticalHeader()->setVisible(false);
    m_recordTable->setObjectName("recordTable");
    layout->addWidget(m_recordTable, 1);

    m_recordHint = new QLabel(QStringLiteral("最多显示最近 10 条报警\n（与单片机环形缓冲区对应）"));
    m_recordHint->setAlignment(Qt::AlignCenter);
    m_recordHint->setObjectName("recordHint");
    layout->addWidget(m_recordHint);

    return panel;
}

// ==================== 信号连接 ====================

void MainWindow::setupConnections()
{
    connect(m_state, &SysState::stateChanged, this, &MainWindow::onStateChanged);
    connect(m_state, &SysState::recordsChanged, this, &MainWindow::onRecordsChanged);

    connect(m_sliderDistance, &QSlider::valueChanged, this, &MainWindow::onSliderDistanceChanged);

    connect(m_btnHumanToggle, &QPushButton::toggled, this, &MainWindow::onToggleHuman);

    connect(m_btnModeMonitor, &QPushButton::clicked, this, &MainWindow::onButtonModeMonitor);
    connect(m_btnModeSilent,  &QPushButton::clicked, this, &MainWindow::onButtonModeSilent);
    connect(m_btnModeRecord,  &QPushButton::clicked, this, &MainWindow::onButtonModeRecord);

    connect(m_spinSafe,  QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onThresholdsChanged);
    connect(m_spinWarn,  QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onThresholdsChanged);
    connect(m_spinForce, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onThresholdsChanged);

    // 串口
    connect(m_btnRefresh, &QPushButton::clicked, this, &MainWindow::onPortsRefresh);
    connect(m_btnConnect, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(m_serial, &SerialBridge::connectedChanged, this, &MainWindow::onSerialConnected);
    connect(m_serial, &SerialBridge::lineReceived, this, &MainWindow::onSerialText);
    connect(m_serial, &SerialBridge::errorText,    this, &MainWindow::onSerialText);
    connect(m_serial, &SerialBridge::statsChanged, this, &MainWindow::onSerialStats);
}

// ==================== 槽函数 ====================

void MainWindow::onSliderDistanceChanged(int value)
{
    m_state->setDistance(double(value));
    m_labelDistanceValue->setText(QString("%1 cm").arg(value));
}

void MainWindow::onButtonModeMonitor() { m_state->setMode(MODE_MONITOR); m_serial->sendModeCommand(0); }
void MainWindow::onButtonModeSilent()  { m_state->setMode(MODE_SILENT);  m_serial->sendModeCommand(1); }
void MainWindow::onButtonModeRecord()  { m_state->setMode(MODE_RECORD);  m_serial->sendModeCommand(2); }

void MainWindow::onToggleHuman(bool checked)
{
    m_state->setHuman(checked);
    m_btnHumanToggle->setText(checked
                              ? QStringLiteral("人体检测：开 ●")
                              : QStringLiteral("人体检测：关 ○"));
}

void MainWindow::onThresholdsChanged()
{
    int safe  = m_spinSafe->value();
    int warn  = m_spinWarn->value();
    int force = m_spinForce->value();

    // 确保层次关系合法
    if (warn >= safe) {
        m_spinWarn->setValue(safe - 5);
        return;
    }
    if (force > safe) {
        m_spinForce->setValue(safe);
        return;
    }
    if (force < warn) {
        m_spinForce->setValue(warn);
        return;
    }

    m_state->setThresholds(safe, warn, force);
}

// ==================== 状态刷新 ====================
// 该函数每 50ms 被调用一次，因此每一项都先比对快照，
// 只有真正变化时才更新控件 / 重新应用样式。

void MainWindow::onStateChanged()
{
    // --- 仪表盘（控件内部也做了去重，值不变不会重绘）---
    m_gauge->setDistance(m_state->distance());
    m_gauge->setAlarmLevel(int(m_state->alarmLevel()));

    // --- 模式 ---
    const int mode = int(m_state->mode());
    if (mode != m_lastMode) {
        m_lastMode = mode;

        switch (mode) {
        case MODE_MONITOR: m_modeLabel->setText(QStringLiteral("监控模式"));
                           m_modeIcon->setProperty("mode", "monitor"); break;
        case MODE_SILENT:  m_modeLabel->setText(QStringLiteral("静默模式"));
                           m_modeIcon->setProperty("mode", "silent");  break;
        default:           m_modeLabel->setText(QStringLiteral("记录查看"));
                           m_modeIcon->setProperty("mode", "record");  break;
        }
        repolish(m_modeIcon);

        m_btnModeMonitor->setProperty("active", mode == MODE_MONITOR);
        m_btnModeSilent->setProperty("active",  mode == MODE_SILENT);
        m_btnModeRecord->setProperty("active",  mode == MODE_RECORD);
        repolish(m_btnModeMonitor);
        repolish(m_btnModeSilent);
        repolish(m_btnModeRecord);
    }

    // --- 人体红外 ---
    const bool human = m_state->humanDetected();
    if (human != m_lastHuman) {
        m_lastHuman = human;
        m_humanLabel->setText(human ? QStringLiteral("检测到人体")
                                    : QStringLiteral("未检测到人体"));
        m_humanLabel->setProperty("active", human);
        repolish(m_humanLabel);
    }

    // --- 三色 LED ---
    const int lvl = int(m_state->alarmLevel());
    if (lvl != m_lastLevel) {
        m_lastLevel = lvl;
        m_ledRed->setText((lvl == LEVEL_ALARM || lvl == LEVEL_FORCE) ? "●" : "○");
        m_ledYellow->setText(lvl == LEVEL_WARN ? "●" : "○");
        m_ledGreen->setText(lvl == LEVEL_SAFE ? "●" : "○");
    }

    // --- 蜂鸣器（变化频率约 200ms 一次）---
    const bool beep = m_state->beepOn();
    if (beep != m_lastBeep) {
        m_lastBeep = beep;
        m_beepIcon->setText(beep ? QStringLiteral("🔊") : QStringLiteral("🔇"));
        m_beepLabel->setText(beep ? QStringLiteral("鸣响中") : QStringLiteral("关闭"));
        m_beepLabel->setProperty("active", beep);
        repolish(m_beepLabel);
    }
}

// 记录表只在新增记录时重建，不再随 50ms 主循环刷新
void MainWindow::onRecordsChanged()
{
    const auto &records = m_state->records();
    const int n = records.size();

    m_recordTable->setRowCount(n);
    for (int i = 0; i < n; ++i) {
        const int row = n - 1 - i;   // 最新的显示在最上面
        const AlarmRecord &r = records.at(i);
        m_recordTable->setItem(row, 0, new QTableWidgetItem(QString("#%1").arg(i + 1)));
        m_recordTable->setItem(row, 1, new QTableWidgetItem(QString::number(r.dist)));
        m_recordTable->setItem(row, 2, new QTableWidgetItem(r.formatTime()));
    }
}

// ==================== 串口相关 ====================

void MainWindow::onPortsRefresh()
{
    m_portCombo->clear();
    m_portCombo->addItems(SerialBridge::availablePorts());
}

void MainWindow::onConnectClicked()
{
    if (m_serial->isConnected()) {
        m_serial->disconnectPort();
        return;
    }
    const QString port = m_portCombo->currentText();
    if (port.isEmpty()) {
        updateSerialStatus();
        m_lastSerialLine = QStringLiteral("没有可用串口，请先刷新或插入USB转串口");
        updateSerialStatus();
        return;
    }
    m_serial->connectPort(port, m_baudCombo->currentText().toInt());
}

void MainWindow::onSerialConnected(bool connected)
{
    m_btnConnect->setText(connected ? QStringLiteral("断开") : QStringLiteral("连接"));
    updateSimControls(connected);
    if (connected)
        m_lastSerialLine = QStringLiteral("已连接，等待数据帧...");
    else
        m_lastSerialLine = QStringLiteral("未连接");
    updateSerialStatus();
}

void MainWindow::onSerialText(const QString &text)
{
    m_lastSerialLine = text;
    updateSerialStatus();
}

void MainWindow::onSerialStats(int ok, int bad)
{
    m_okFrames = ok;
    m_badFrames = bad;
    updateSerialStatus();
}

void MainWindow::updateSerialStatus()
{
    m_serialStatus->setText(QStringLiteral("OK %1 / BAD %2 | %3")
                                .arg(m_okFrames).arg(m_badFrames)
                                .arg(m_lastSerialLine));
}

// 串口接管后禁用本地模拟控件，避免两个数据源打架
void MainWindow::updateSimControls(bool serialActive)
{
    m_sliderDistance->setEnabled(!serialActive);
    m_btnHumanToggle->setEnabled(!serialActive);
}
