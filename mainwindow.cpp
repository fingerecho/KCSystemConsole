#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include <QVBoxLayout>
#include <QHeaderView>
#include <QScrollBar>
#include <QLocale>

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
                } else if(button == ui->settingToolButton) {
                    ui->mainContentStackedWidget->setCurrentWidget(ui->settingPage);
                }
            });

    // 强制布局计算一次，确保 leftMenuWidget 获得正确的初始尺寸
    ui->leftMenuWidget->layout()->activate();
    m_expandedWidth = ui->leftMenuWidget->width();

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
    m_processTable->setHorizontalHeaderLabels({"名称", "PID", "CPU", "内存", "磁盘", "网络"});

    m_processTable->horizontalHeader()->setStretchLastSection(true);
    m_processTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_processTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_processTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_processTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Interactive);
    m_processTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Interactive);
    m_processTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Interactive);

    m_processTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_processTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_processTable->verticalHeader()->setVisible(false);
    m_processTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_processTable->setAlternatingRowColors(true);

    QTimer::singleShot(0, this, [this]() {
        setColumnProportions({6, 1, 1, 1, 2, 1});
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
    int availableWidth = m_processTable->viewport()->width() - 5;

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
    int scrollPos = m_processTable->verticalScrollBar()->value();
    int newCount = processes.size();
    int oldCount = m_processTable->rowCount();

    m_processTable->setUpdatesEnabled(false);

    // Expand rows if needed (setRowCount upward only adds empty rows, no items created)
    if (newCount > oldCount)
        m_processTable->setRowCount(newCount);

    for (int row = 0; row < newCount; ++row) {
        const auto &p = processes[row];

        // Helper: reuse existing item or create one with right-alignment
        auto ensureItem = [&](int col) -> QTableWidgetItem * {
            auto *it = m_processTable->item(row, col);
            if (!it) {
                it = new QTableWidgetItem();
                it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                m_processTable->setItem(row, col, it);
            }
            return it;
        };

        // col 0: Name (default left-aligned)
        auto *nameItem = m_processTable->item(row, 0);
        if (!nameItem) {
            nameItem = new QTableWidgetItem();
            m_processTable->setItem(row, 0, nameItem);
        }
        nameItem->setText(p.name);

        // col 1-5: Data columns
        ensureItem(1)->setText(QString::number(p.pid));
        ensureItem(2)->setText(QString::number(p.cpuPercent, 'f', 1) + "%");
        ensureItem(3)->setText(formatMemoryKB(p.memoryKB));
        ensureItem(4)->setText(QString("R:%1  W:%2")
                                   .arg(formatBytes(p.diskReadRate))
                                   .arg(formatBytes(p.diskWriteRate)));
        ensureItem(5)->setText(QString::number(p.networkConnections));
    }

    // Shrink if needed
    if (newCount < oldCount)
        m_processTable->setRowCount(newCount);

    m_processTable->setUpdatesEnabled(true);
    m_processTable->verticalScrollBar()->setValue(scrollPos);
}
