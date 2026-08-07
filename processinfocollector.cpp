#include "processinfocollector.h"

#include <tlhelp32.h>
#include <psapi.h>
#include <iphlpapi.h>
#include <tcpmib.h>
#include <udpmib.h>
#include <vector>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "iphlpapi.lib")

ProcessInfoCollector::ProcessInfoCollector() = default;

ULARGE_INTEGER ProcessInfoCollector::fileTimeToULarge(const FILETIME &ft)
{
    ULARGE_INTEGER ul;
    ul.LowPart = ft.dwLowDateTime;
    ul.HighPart = ft.dwHighDateTime;
    return ul;
}

int ProcessInfoCollector::countNetworkConnections(DWORD pid)
{
    int count = 0;

    // ---- TCP ----
    DWORD tcpSize = 0;
    GetExtendedTcpTable(nullptr, &tcpSize, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    if (tcpSize > 0) {
        std::vector<BYTE> buf(tcpSize);
        auto *tcp = reinterpret_cast<MIB_TCPTABLE_OWNER_PID *>(buf.data());
        if (GetExtendedTcpTable(tcp, &tcpSize, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) == NO_ERROR) {
            for (DWORD i = 0; i < tcp->dwNumEntries; ++i) {
                if (tcp->table[i].dwOwningPid == pid)
                    ++count;
            }
        }
    }

    // ---- UDP ----
    DWORD udpSize = 0;
    GetExtendedUdpTable(nullptr, &udpSize, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0);
    if (udpSize > 0) {
        std::vector<BYTE> buf(udpSize);
        auto *udp = reinterpret_cast<MIB_UDPTABLE_OWNER_PID *>(buf.data());
        if (GetExtendedUdpTable(udp, &udpSize, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0) == NO_ERROR) {
            for (DWORD i = 0; i < udp->dwNumEntries; ++i) {
                if (udp->table[i].dwOwningPid == pid)
                    ++count;
            }
        }
    }

    return count;
}

QList<ProcessInfo> ProcessInfoCollector::collect()
{
    QList<ProcessInfo> result;
    QMap<DWORD, ProcessSample> currentSamples;

    // ---- Current wall-clock time (100 ns units) ----
    FILETIME sysFt;
    GetSystemTimeAsFileTime(&sysFt);
    ULARGE_INTEGER now = fileTimeToULarge(sysFt);

    // ---- Enumerate processes ----
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return result;

    PROCESSENTRY32 pe{};
    pe.dwSize = sizeof(pe);

    if (Process32First(snap, &pe)) {
        do {
            DWORD pid = pe.th32ProcessID;
            QString name = QString::fromWCharArray(pe.szExeFile);

            ProcessInfo info;
            info.name = name;
            info.pid = pid;
            info.cpuPercent = 0.0;
            info.memoryKB = 0;
            info.diskReadRate = 0;
            info.diskWriteRate = 0;
            info.networkConnections = 0;

            ProcessSample sample{};
            sample.hasDiskData = false;

            HANDLE hProc = OpenProcess(
                PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                FALSE, pid);

            if (hProc) {
                // ---- CPU times ----
                FILETIME createFt, exitFt, kernelFt, userFt;
                if (GetProcessTimes(hProc, &createFt, &exitFt, &kernelFt, &userFt)) {
                    sample.kernelTime = fileTimeToULarge(kernelFt);
                    sample.userTime = fileTimeToULarge(userFt);
                    sample.sampleTime = now;

                    // Calculate CPU % from previous sample
                    if (m_prevSamples.contains(pid)) {
                        const auto &prev = m_prevSamples[pid];
                        ULONGLONG deltaProc =
                            (sample.kernelTime.QuadPart - prev.kernelTime.QuadPart)
                            + (sample.userTime.QuadPart - prev.userTime.QuadPart);
                        ULONGLONG deltaWall = sample.sampleTime.QuadPart - prev.sampleTime.QuadPart;

                        if (deltaWall > 0) {
                            info.cpuPercent = (static_cast<double>(deltaProc) / deltaWall) * 100.0;
                        }
                    }
                }

                // ---- Memory ----
                PROCESS_MEMORY_COUNTERS_EX pmc{};
                pmc.cb = sizeof(pmc);
                if (GetProcessMemoryInfo(hProc,
                        reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc), sizeof(pmc))) {
                    info.memoryKB = static_cast<qint64>(pmc.WorkingSetSize / 1024);
                }

                // ---- Disk I/O ----
                IO_COUNTERS io{};
                if (GetProcessIoCounters(hProc, &io)) {
                    sample.readTransferCount.QuadPart = io.ReadTransferCount;
                    sample.writeTransferCount.QuadPart = io.WriteTransferCount;
                    sample.hasDiskData = true;

                    if (m_prevSamples.contains(pid) && m_prevSamples[pid].hasDiskData) {
                        const auto &prev = m_prevSamples[pid];
                        ULONGLONG deltaWall = sample.sampleTime.QuadPart - prev.sampleTime.QuadPart;
                        if (deltaWall > 0) {
                            double secs = deltaWall / 1.0e7;  // 100 ns → seconds
                            info.diskReadRate = static_cast<qint64>(
                                (sample.readTransferCount.QuadPart - prev.readTransferCount.QuadPart) / secs);
                            info.diskWriteRate = static_cast<qint64>(
                                (sample.writeTransferCount.QuadPart - prev.writeTransferCount.QuadPart) / secs);
                        }
                    }
                }

                CloseHandle(hProc);
            }

            // ---- Network (doesn't need process handle) ----
            info.networkConnections = countNetworkConnections(pid);

            currentSamples[pid] = sample;
            result.append(info);

        } while (Process32Next(snap, &pe));
    }

    CloseHandle(snap);

    // Update previous samples for next call
    m_prevSamples = currentSamples;

    return result;
}
