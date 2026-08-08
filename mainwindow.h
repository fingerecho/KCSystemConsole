#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStyle>
#include <QButtonGroup>
#include <QTableWidget>
#include <QTimer>
#include <QThread>
#include <QVector>
#include "processinfocollector.h"
#include "TwoRowHeaderView.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void on_collapseToolButton_pressed();
    void onProcessCollected(QList<ProcessInfo> processes);

private:
    void applyMenuAlignment(bool expanded);
    void initLeftMenuBtnGrp();
    void initProcessPage();
    void setColumnProportions(const QVector<int>& proportions);
    void updateHeaderSummary(const QList<ProcessInfo> &processes);
    static qint64 getTotalSystemMemoryKB();

private:
    QButtonGroup *m_leftMenuButtonGroup;
    QTableWidget *m_processTable;
    TwoRowHeaderView *m_processHeader;
    QTimer *m_processTimer;
    QThread *m_processThread;
    ProcessInfoCollector *m_processCollector;

    int m_expandedWidth;
    int m_collapsedWidth;
    bool m_menuExpanded = false;

    Ui::MainWindow *ui;
};

#endif // MAINWINDOW_H

