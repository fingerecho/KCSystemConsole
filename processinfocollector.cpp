#include "processinfocollector.h"

#include <tlhelp32.h>
#include <psapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <tcpmib.h>
#include <udpmib.h>
#include <vector>

// MinGW may not define AF_INET6; ensure it's available
#ifndef AF_INET6
#define AF_INET6 23
#endif

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "iphlpapi.lib")

// ============================================================
// QueryWorkingSet – iterates every page in the process working
// set and counts pages where Shared==0 (truly private pages
// backed by commit, not file-backed like DLLs).
// Returns private working set in KB.  0 on failure.
// Requires PROCESS_VM_READ.
// ============================================================
static qint64 getPrivateWorkingSetKB(HANDLE hProc)
{
    static DWORD s_pageSize = 0;
    if (s_pageSize == 0) {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        s_pageSize = si.dwPageSize;
    }

    // Start with room for 8192 pages (~32 MB working set)
    DWORD bufSize = sizeof(PSAPI_WORKING_SET_INFORMATION)
                    + 8192 * sizeof(PSAPI_WORKING_SET_BLOCK);
    std::vector<BYTE> buf(bufSize);

    for (int attempt = 0; attempt < 3; ++attempt) {
        auto *wsi = reinterpret_cast<PSAPI_WORKING_SET_INFORMATION *>(buf.data());

        if (QueryWorkingSet(hProc, buf.data(), bufSize)) {
            SIZE_T privatePages = 0;
            for (ULONG_PTR i = 0; i < wsi->NumberOfEntries; ++i) {
                if (!wsi->WorkingSetInfo[i].Shared)
                    ++privatePages;
            }
            return static_cast<qint64>((privatePages * s_pageSize) / 1024);
        }

        if (GetLastError() != ERROR_BAD_LENGTH)
            return 0;   // genuine failure (access denied, etc.)

        // Buffer too small – retry with the count the kernel told us
        bufSize = static_cast<DWORD>(sizeof(PSAPI_WORKING_SET_INFORMATION)
                    + wsi->NumberOfEntries * sizeof(PSAPI_WORKING_SET_BLOCK));
        buf.resize(bufSize);
    }
    return 0;
}

ProcessInfoCollector::ProcessInfoCollector(QObject *parent)
    : QObject(parent)
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    m_logicalProcessorCount = static_cast<int>(si.dwNumberOfProcessors);
    if (m_logicalProcessorCount < 1)
        m_logicalProcessorCount = 1;
}

ULARGE_INTEGER ProcessInfoCollector::fileTimeToULarge(const FILETIME &ft)
{
    ULARGE_INTEGER ul;
    ul.LowPart = ft.dwLowDateTime;
    ul.HighPart = ft.dwHighDateTime;
    return ul;
}

QHash<DWORD, int> ProcessInfoCollector::buildNetworkMap()
{
    QHash<DWORD, int> map;

    // ---- TCP IPv4: only ESTABLISHED connections ----
    DWORD tcpSize = 0;
    GetExtendedTcpTable(nullptr, &tcpSize, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    if (tcpSize > 0) {
        std::vector<BYTE> buf(tcpSize);
        auto *tcp = reinterpret_cast<MIB_TCPTABLE_OWNER_PID *>(buf.data());
        if (GetExtendedTcpTable(tcp, &tcpSize, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) == NO_ERROR) {
            for (DWORD i = 0; i < tcp->dwNumEntries; ++i) {
                if (tcp->table[i].dwState == MIB_TCP_STATE_ESTAB)
                    map[tcp->table[i].dwOwningPid]++;
            }
        }
    }

    // ---- TCP IPv6: only ESTABLISHED connections ----
    DWORD tcp6Size = 0;
    GetExtendedTcpTable(nullptr, &tcp6Size, FALSE, AF_INET6, TCP_TABLE_OWNER_PID_ALL, 0);
    if (tcp6Size > 0) {
        std::vector<BYTE> buf(tcp6Size);
        auto *tcp6 = reinterpret_cast<MIB_TCP6TABLE_OWNER_PID *>(buf.data());
        if (GetExtendedTcpTable(tcp6, &tcp6Size, FALSE, AF_INET6, TCP_TABLE_OWNER_PID_ALL, 0) == NO_ERROR) {
            for (DWORD i = 0; i < tcp6->dwNumEntries; ++i) {
                if (tcp6->table[i].dwState == MIB_TCP_STATE_ESTAB)
                    map[tcp6->table[i].dwOwningPid]++;
            }
        }
    }

    // ---- UDP IPv4 ----
    DWORD udpSize = 0;
    GetExtendedUdpTable(nullptr, &udpSize, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0);
    if (udpSize > 0) {
        std::vector<BYTE> buf(udpSize);
        auto *udp = reinterpret_cast<MIB_UDPTABLE_OWNER_PID *>(buf.data());
        if (GetExtendedUdpTable(udp, &udpSize, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0) == NO_ERROR) {
            for (DWORD i = 0; i < udp->dwNumEntries; ++i)
                map[udp->table[i].dwOwningPid]++;
        }
    }

    // ---- UDP IPv6 ----
    DWORD udp6Size = 0;
    GetExtendedUdpTable(nullptr, &udp6Size, FALSE, AF_INET6, UDP_TABLE_OWNER_PID, 0);
    if (udp6Size > 0) {
        std::vector<BYTE> buf(udp6Size);
        auto *udp6 = reinterpret_cast<MIB_UDP6TABLE_OWNER_PID *>(buf.data());
        if (GetExtendedUdpTable(udp6, &udp6Size, FALSE, AF_INET6, UDP_TABLE_OWNER_PID, 0) == NO_ERROR) {
            for (DWORD i = 0; i < udp6->dwNumEntries; ++i)
                map[udp6->table[i].dwOwningPid]++;
        }
    }

    return map;
}

QList<ProcessInfo> ProcessInfoCollector::collect()
{
    QList<ProcessInfo> result;
    QMap<DWORD, ProcessSample> currentSamples;

    // ---- Build PID→connection-count map ONCE ----
    QHash<DWORD, int> netMap = buildNetworkMap();

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

            // Try VM_READ first (needed for QueryWorkingSet).  If denied,
            // fall back to QUERY_INFORMATION only (CPU + disk still work).
            HANDLE hProcVm = OpenProcess(
                PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
            HANDLE hProc = hProcVm;
            if (!hProc)
                hProc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);

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
                            info.cpuPercent = (static_cast<double>(deltaProc)
                                               / (deltaWall * m_logicalProcessorCount)) * 100.0;
                        }
                    }
                }

                // ---- Memory ----
                if (hProcVm) {
                    // Preferred: private working set via QueryWorkingSet
                    qint64 ws = getPrivateWorkingSetKB(hProcVm);
                    if (ws > 0)
                        info.memoryKB = ws;
                }
                if (info.memoryKB == 0) {
                    // Fallback: total working set (e.g. protected processes)
                    PROCESS_MEMORY_COUNTERS_EX pmc{};
                    pmc.cb = sizeof(pmc);
                    if (GetProcessMemoryInfo(hProc,
                            reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc), sizeof(pmc)))
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
                if (hProcVm && hProcVm != hProc)
                    CloseHandle(hProcVm);
            }

            // ---- Network (O(1) lookup from pre-built map) ----
            info.networkConnections = netMap.value(pid, 0);

            currentSamples[pid] = sample;
            result.append(info);

        } while (Process32Next(snap, &pe));
    }

    CloseHandle(snap);

    // Update previous samples for next call
    m_prevSamples = currentSamples;

    return result;
}

void ProcessInfoCollector::doCollect()
{
    emit collected(collect());
}