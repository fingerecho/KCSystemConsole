#ifndef PROCESSINFOCOLLECTOR_H
#define PROCESSINFOCOLLECTOR_H

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include <QHash>
#include <windows.h>

struct ProcessInfo
{
    QString name;
    DWORD pid;
    double cpuPercent;        // CPU usage % (can exceed 100% for multi-threaded)
    qint64 memoryKB;          // Working set in KB
    qint64 diskReadRate;      // Disk read rate (bytes/sec)
    qint64 diskWriteRate;     // Disk write rate (bytes/sec)
    int networkConnections;   // TCP + UDP connections
};

class ProcessInfoCollector : public QObject
{
    Q_OBJECT

public:
    explicit ProcessInfoCollector(QObject *parent = nullptr);

public slots:
    void doCollect();

signals:
    void collected(QList<ProcessInfo> processes);
private:
    int countNetworkConnections(DWORD pid);
private:
    QList<ProcessInfo> collect();
    QHash<DWORD, int> buildNetworkMap();

    struct ProcessSample
    {
        ULARGE_INTEGER kernelTime;
        ULARGE_INTEGER userTime;
        ULARGE_INTEGER readTransferCount;
        ULARGE_INTEGER writeTransferCount;
        ULARGE_INTEGER sampleTime;
        bool hasDiskData;
    };

    QMap<DWORD, ProcessSample> m_prevSamples;

    static ULARGE_INTEGER fileTimeToULarge(const FILETIME &ft);
};

#endif // PROCESSINFOCOLLECTOR_H
