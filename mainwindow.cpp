#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "NumericTableItem.h"

#include <QVBoxLayout>
#include <QHeaderView>
#include <QScrollBar>
#include <QLocale>
#include <windows.h>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    initLeftMenuBtnGrp();
    initProcessPage();
}

MainWindow::~MainWindow()
{
    m_processTimer->stop();
    m_processThread->quit();
    m_processThread->wait();
    delete ui;
}

void MainWindow::initLeftMenuBtnGrp()
{
    m_leftMenuButtonGroup = new QButtonGroup(this);

    m_leftMenuButtonGroup->addButton(ui->processToolButton);
    m_leftMenuButtonGroup->addButton(ui->settingToolButton);

    connect(m_leftMenuButtonGroup, QOverload<QAbstractButton*>::of(&QButtonGroup::buttonClicked),
            this, [=](QAbstractButton *button){
                if(button == ui->processToolButton) {
                    ui->mainContentStackedWidget->setCurrentWidget(ui->processPage);
                    ui->topBannerStackedWidget->setCurrentWidget(ui->processSearchBar);
                } else if(button == ui->settingToolButton) {
                    ui->mainContentStackedWidget->setCurrentWidget(ui->settingPage);
                    ui->topBannerStackedWidget->setCurrentWidget(ui->blankBar);
                }
            });

    // 强制布局计算一次，确保 leftMenuWidget 获得正确的初始尺寸
    // 旧代码（删除）：
    //ui->leftMenuWidget->layout()->activate();
    //m_expandedWidth = ui->leftMenuWidget->width();

    // 新代码：
    QFontMetrics fm(ui->processToolButton->font());
    int iconW = ui->collapseToolButton->iconSize().width();
    int textW = qMax(fm.horizontalAdvance("  进程"), fm.horizontalAdvance("  设置"));
    m_expandedWidth = qMax(iconW + textW + 32, 100);  // 图标+文字+内边距，最低100px


    // 折叠宽度 = 图标尺寸 + 左右内边距（假设与 collapseToolButton 相同）
    const int iconWidth = ui->collapseToolButton->iconSize().width();
    // 内边距可以从样式表中读取，这里简单取 24（= 12+12）
    m_collapsedWidth = ui->collapseToolButton->sizeHint().width();

    applyMenuAlignment(m_menuExpanded);   // 见下方辅助函数

}

void MainWindow::on_collapseToolButton_pressed()
{
    m_menuExpanded = !m_menuExpanded;
    applyMenuAlignment(m_menuExpanded);
}

void MainWindow::applyMenuAlignment(bool expanded)
{
    QLayout* layout = ui->leftMenuWidget->layout();
    if (!layout) return;

    // 设置菜单宽度
    ui->leftMenuWidget->setFixedWidth(expanded ? m_expandedWidth : m_collapsedWidth);
    ui->leftMenuWidget->setProperty("expanded",expanded);
    ui->leftMenuWidget->style()->polish(ui->leftMenuWidget);

    //遍历布局中的每个 QToolButton
    for (int i = 0; i < layout->count(); ++i) {
        QLayoutItem* item = layout->itemAt(i);
        if (!item->widget()) continue;

        QToolButton* btn = qobject_cast<QToolButton*>(item->widget());
        if (!btn) continue;

        if (expanded) {
            // 展开：填满宽度，文本在图标右侧
            layout->setAlignment(btn, Qt::AlignHCenter | Qt::AlignVCenter);
            btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            btn->setMinimumWidth(m_expandedWidth*0.92);
            btn->setMaximumWidth(QWIDGETSIZE_MAX);

            if(btn->objectName()=="processToolButton"){
                btn->setText("  进程");
            }else if(btn->objectName()=="settingToolButton"){
                btn->setText("  设置");
            }else if(btn->objectName()=="collapseToolButton"){
                btn->setText("  ");
            }

        } else {
            // 折叠：按内容自适应，仅图标居中对齐
            layout->setAlignment(btn, Qt::AlignCenter | Qt::AlignVCenter);
            btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
            btn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
            btn->setMinimumWidth(0);
            btn->setMaximumWidth(m_collapsedWidth*0.86);

            btn->setText("");
        }
    }
    ui->leftMenuWidget->update();

}

void MainWindow::initProcessPage()
{
    // ---- Table ----
    m_processTable = new QTableWidget(0, 6, ui->processPage);

    // Replace default header with two-row header
    m_processHeader = new TwoRowHeaderView(Qt::Horizontal, m_processTable);
    m_processTable->setHorizontalHeader(m_processHeader);
    m_processTable->setHorizontalHeaderLabels({"进程名称", "PID", "CPU", "内存", "硬盘", "连接数"});

    m_processHeader->setStretchLastSection(true);
    m_processHeader->setSectionResizeMode(0, QHeaderView::Interactive);
    m_processHeader->setSectionResizeMode(1, QHeaderView::Interactive);
    m_processHeader->setSectionResizeMode(2, QHeaderView::Interactive);
    m_processHeader->setSectionResizeMode(3, QHeaderView::Interactive);
    m_processHeader->setSectionResizeMode(4, QHeaderView::Interactive);
    m_processHeader->setSectionResizeMode(5, QHeaderView::Interactive);

    m_processTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_processTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_processTable->verticalHeader()->setVisible(false);
    m_processTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_processTable->setAlternatingRowColors(true);
    m_processTable->setStyleSheet(
        "QTableWidget { alternate-background-color: #fbfbfb; }"
        "QTableWidget::item:selected { background-color: #f6f6f6; color: inherit; }"
        "QTableWidget::item:hover { background-color: #f6f6f6; }");

    m_processTable->setSortingEnabled(true);

    QTimer::singleShot(0, this, [this]() {
        setColumnProportions({55, 10, 10, 14, 25, 10});
    });
    // ---- Layout ----
    auto *layout = new QVBoxLayout(ui->processPage);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_processTable);

    // ---- Collector, Thread, Timer ----
    m_processCollector = new ProcessInfoCollector();          // no parent – will be moved
    m_processThread = new QThread(this);
    m_processCollector->moveToThread(m_processThread);

    connect(m_processThread, &QThread::finished,
            m_processCollector, &QObject::deleteLater);

    m_processTimer = new QTimer(this);
    m_processTimer->setInterval(5000);

    // Main-thread timer → worker-thread slot  (queued cross-thread)
    connect(m_processTimer, &QTimer::timeout,
            m_processCollector, &ProcessInfoCollector::doCollect);

    // Worker-thread signal → main-thread slot  (queued cross-thread)
    connect(m_processCollector, &ProcessInfoCollector::collected,
            this, &MainWindow::onProcessCollected);

    m_processThread->start();

    // Trigger immediate first collect (queued to worker thread)
    QMetaObject::invokeMethod(m_processCollector, "doCollect", Qt::QueuedConnection);

    m_processTimer->start();
}

void MainWindow::setColumnProportions(const QVector<int>& proportions) {
    if (!m_processTable) return;

    // 获取表格可视区域的宽度（减去边框和边距）
    int availableWidth = m_processTable->viewport()->width() - 10;

    // 计算总比例
    int total = std::accumulate(proportions.begin(), proportions.end(), 0);

    // 按比例设置列宽
    for (size_t i = 0; i < proportions.size() && i < 6; ++i) {
        int width = availableWidth * proportions[i] / total;
        m_processTable->setColumnWidth(i, width);
    }
}

static QString formatBytes(qint64 bytesPerSec)
{
    if (bytesPerSec < 1024)
        return QString::number(bytesPerSec) + " B/s";
    if (bytesPerSec < 1024 * 1024)
        return QString::number(bytesPerSec / 1024.0, 'f', 1) + " KB/s";
    return QString::number(bytesPerSec / (1024.0 * 1024.0), 'f', 2) + " MB/s";
}

static QString formatMemoryKB(qint64 kb)
{
    if (kb < 1024)
        return QString::number(kb) + " KB";
    if (kb < 1024 * 1024)
        return QString::number(kb / 1024.0, 'f', 1) + " MB";
    return QString::number(kb / (1024.0 * 1024.0), 'f', 2) + " GB";
}

void MainWindow::onProcessCollected(QList<ProcessInfo> processes)
{
    // Save current sort state before disabling (setSortingEnabled clears it)
    const bool wasSorting = m_processTable->isSortingEnabled();
    const int sortCol = m_processHeader->sortIndicatorSection();
    const Qt::SortOrder sortOrder = m_processHeader->sortIndicatorOrder();
    const bool hadSortIndicator = m_processHeader->isSortIndicatorShown();

    // Disable sorting during data population to avoid blank rows
    m_processTable->setSortingEnabled(false);

    int scrollPos = m_processTable->verticalScrollBar()->value();
    int newCount = processes.size();
    int oldCount = m_processTable->rowCount();

    m_processTable->setUpdatesEnabled(false);

    // Expand rows if needed (setRowCount upward only adds empty rows, no items created)
    if (newCount > oldCount)
        m_processTable->setRowCount(newCount);

    for (int row = 0; row < newCount; ++row) {
        const auto &p = processes[row];

        // Helper: get or create a NumericTableItem for numeric columns
        auto ensureNumeric = [&](int col, double sortKey) -> NumericTableItem * {
            auto *it = dynamic_cast<NumericTableItem *>(m_processTable->item(row, col));
            if (!it) {
                it = new NumericTableItem(sortKey);
                it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                m_processTable->setItem(row, col, it);
            } else {
                it->setSortKey(sortKey);
            }
            return it;
        };

        // col 0: Name (default left-aligned, plain QTableWidgetItem)
        auto *nameItem = m_processTable->item(row, 0);
        if (!nameItem) {
            nameItem = new QTableWidgetItem();
            m_processTable->setItem(row, 0, nameItem);
        }
        nameItem->setText(p.name);

        // col 1-5: numeric columns with sort keys
        ensureNumeric(1, static_cast<double>(p.pid))
            ->setText(QString::number(p.pid));
        ensureNumeric(2, p.cpuPercent)
            ->setText(QString::number(p.cpuPercent, 'f', 1) + "%");
        ensureNumeric(3, static_cast<double>(p.memoryKB))
            ->setText(formatMemoryKB(p.memoryKB));
        ensureNumeric(4, static_cast<double>(p.diskReadRate + p.diskWriteRate))
            ->setText(QString("R:%1  W:%2")
                          .arg(formatBytes(p.diskReadRate))
                          .arg(formatBytes(p.diskWriteRate)));
        ensureNumeric(5, static_cast<double>(p.networkConnections))
            ->setText(QString::number(p.networkConnections));
    }

    // Shrink if needed
    if (newCount < oldCount)
        m_processTable->setRowCount(newCount);

    m_processTable->setUpdatesEnabled(true);

    // Re-enable sorting and restore previous sort state
    if (wasSorting) {
        m_processTable->setSortingEnabled(true);
        if (hadSortIndicator && sortCol >= 0 && sortCol < m_processTable->columnCount()) {
            m_processTable->sortItems(sortCol, sortOrder);
        }
    }

    m_processTable->verticalScrollBar()->setValue(scrollPos);

    updateHeaderSummary(processes);
}

void MainWindow::updateHeaderSummary(const QList<ProcessInfo> &processes)
{
    const int count = processes.size();
    double totalCpu = 0.0;
    qint64 totalMemKB = 0;
    qint64 totalDiskRate = 0;   // read + write bytes/sec
    int totalConnections = 0;

    for (const auto &p : processes) {
        totalCpu += p.cpuPercent;
        totalMemKB += p.memoryKB;
        totalDiskRate += p.diskReadRate + p.diskWriteRate;
        totalConnections += p.networkConnections;
    }

    // Memory percentage: use system-wide "in use" (matches Task Manager)
    double memPercent = 0.0;
    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(memStatus);
    if (GlobalMemoryStatusEx(&memStatus)) {
        qint64 usedKB = static_cast<qint64>((memStatus.ullTotalPhys - memStatus.ullAvailPhys) / 1024);
        memPercent = (memStatus.ullTotalPhys > 0)
            ? (static_cast<double>(usedKB) / (memStatus.ullTotalPhys / 1024)) * 100.0
            : 0.0;
        // Also update per-process memory sum so formatMemoryKB can display it correctly
        totalMemKB = usedKB;
    }

    // Disk percentage: relative to 100 MB/s baseline
    constexpr double kDiskBaseline = 100.0 * 1024.0 * 1024.0; // 100 MB/s
    const double diskPercent = (totalDiskRate / kDiskBaseline) * 100.0;

    // Network percentage: relative to 1000 connections baseline
    constexpr double kNetBaseline = 1000.0;
    const double netPercent = (totalConnections / kNetBaseline) * 100.0;

    m_processHeader->setTopLabels({
        QString::number(count),                        // col 0: process count
        QString(),                                      // col 1: PID – empty
        QString::number(totalCpu, 'f', 1) + "%",       // col 2: total CPU%
        QString::number(memPercent, 'f', 1) + "%",     // col 3: total memory%
        QString::number(diskPercent, 'f', 1) + "%",    // col 4: total disk%
        QString::number(netPercent, 'f', 1) + "%"      // col 5: total network%
    });
}

qint64 MainWindow::getTotalSystemMemoryKB()
{
    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(memStatus);
    if (GlobalMemoryStatusEx(&memStatus))
        return static_cast<qint64>(memStatus.ullTotalPhys / 1024);
    return 0;
}
