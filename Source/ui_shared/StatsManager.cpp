#include "StatsManager.h"
#include "string_format.h"
#include "PS2VM.h"

void CStatsManager::OnNewFrame(CPS2VM* virtualMachine)
{
    {
        std::lock_guard<std::mutex> statsLock(m_statsMutex);

        auto cpuUtilisation = virtualMachine->GetCpuUtilisationInfo();

        m_cpuUtilisation.eeTotalTicks += cpuUtilisation.eeTotalTicks;
        m_cpuUtilisation.eeIdleTicks += cpuUtilisation.eeIdleTicks;
        m_cpuUtilisation.iopTotalTicks += cpuUtilisation.iopTotalTicks;
        m_cpuUtilisation.iopIdleTicks += cpuUtilisation.iopIdleTicks;
    }
    
#ifdef PROFILE
    {
        std::lock_guard<std::mutex> profileLock(m_profilerZonesMutex);

        auto zones = CProfiler::GetInstance().GetStats();

        for(const auto& zone : zones)
        {
            auto& zoneInfo = m_profilerZones[zone.name];

            zoneInfo.currentValue += zone.totalTime;

            if(zone.totalTime != 0)
            {
                zoneInfo.minValue =
                    std::min<uint64>(zoneInfo.minValue, zone.totalTime);
            }

            zoneInfo.maxValue =
                std::max<uint64>(zoneInfo.maxValue, zone.totalTime);
        }
    }
#endif
}

void CStatsManager::RecordGsRegisterWrite()
{
    ++m_gsRegisterWrites;
}

void CStatsManager::RecordGsVertexKick(bool drawingKick)
{
    ++m_gsVertexKicks;

    if(drawingKick)
    {
        ++m_gsDrawingKicks;
    }
}

void CStatsManager::RecordGsBufferedVertices(uint32 count)
{
    m_gsBufferedVertices += count;
}

void CStatsManager::RecordGsBufferFlush()
{
    ++m_gsBufferFlushes;
}

void CStatsManager::RecordGsDrawSubmission()
{
    ++m_gsDrawSubmissions;
}

std::string CStatsManager::GetGsPipelineDiagnostics() const
{
    return string_format(
        "GS register writes: %llu\n"
        "GS vertex kicks: %llu\n"
        "GS drawing kicks: %llu\n"
        "GS vertices buffered: %llu\n"
        "GS buffer flushes: %llu\n"
        "OpenGL draw submissions: %llu\n"
        "GIF FIFO writes: %llu\n"
        "GIF packets processed: %llu\n"
        "GIF DMA QWC received: %llu\n"
        "GIF FIFO stall events: %llu\n",

        static_cast<unsigned long long>(m_gsRegisterWrites.load()),
        static_cast<unsigned long long>(m_gsVertexKicks.load()),
        static_cast<unsigned long long>(m_gsDrawingKicks.load()),
        static_cast<unsigned long long>(m_gsBufferedVertices.load()),
        static_cast<unsigned long long>(m_gsBufferFlushes.load()),
        static_cast<unsigned long long>(m_gsDrawSubmissions.load()),

        static_cast<unsigned long long>(m_gifFifoWrites.load()),
        static_cast<unsigned long long>(m_gifPackets.load()),
        static_cast<unsigned long long>(m_gifDmaQwc.load()),
        static_cast<unsigned long long>(m_gifFifoStalls.load())
    );
}

void CStatsManager::RecordCpuExecution(
    bool isEe,
    uint64 ticks,
    uint32 pc,
    bool idle)
{
    auto& totalTicks = isEe
        ? m_eeExecutionTicks
        : m_iopExecutionTicks;

    auto& batches = isEe
        ? m_eeExecutionBatches
        : m_iopExecutionBatches;

    auto& idleTicks = isEe
        ? m_eeIdleTicks
        : m_iopIdleTicks;

    auto& firstPc = isEe
        ? m_eeFirstPc
        : m_iopFirstPc;

    auto& lastPc = isEe
        ? m_eeLastPc
        : m_iopLastPc;

    auto& lastIdle = isEe
        ? m_eeLastIdle
        : m_iopLastIdle;

    totalTicks.fetch_add(ticks);
    batches.fetch_add(1);

    if(idle)
    {
        idleTicks.fetch_add(ticks);
    }

    uint32 expected = 0xFFFFFFFF;

    firstPc.compare_exchange_strong(
        expected,
        pc);

    lastPc.store(pc);
    lastIdle.store(idle ? 1 : 0);
}

void CStatsManager::RecordCdvdRequest(
    uint32 type,
    uint32 firstSector,
    uint32 sectorCount)
{
    if(type >= 3) return;

    m_cdvdRequests[type].fetch_add(1);

    m_cdvdRequestedSectors[type].fetch_add(
        sectorCount);

    uint32 expected = 0xFFFFFFFF;

    m_cdvdFirstSector[type].compare_exchange_strong(
        expected,
        firstSector);

    m_cdvdLastSector[type].store(firstSector);
}

void CStatsManager::RecordCdvdReadSuccess(
    uint32 type,
    uint64 bytes)
{
    if(type >= 3) return;

    m_cdvdSuccessfulBytes[type].fetch_add(bytes);
}

void CStatsManager::RecordCdvdFailure(
    uint32 type,
    bool rejected)
{
    if(type >= 3) return;

    m_cdvdFailures[type].fetch_add(1);

    if(rejected)
    {
        m_cdvdRejected[type].fetch_add(1);
    }
}

void CStatsManager::RecordGsPrimitive(
    uint32 primitiveType,
    uint32 vertexCount)
{
    if(primitiveType >= 7) return;

    m_gsPrimitiveCounts[primitiveType].fetch_add(1);

    m_gsPrimitiveVertices[primitiveType].fetch_add(
        vertexCount);
}

void CStatsManager::RecordGsEmptyBuffer()
{
    m_gsEmptyBufferEvents.fetch_add(1);
}

void CStatsManager::RecordGsRenderingSkipped(
    bool drawDisabled)
{
    if(drawDisabled)
    {
        m_gsDrawDisabledEvents.fetch_add(1);
    }
}

void CStatsManager::OnGsNewFrame(uint32 drawCalls)
{
    std::lock_guard<std::mutex> statsLock(m_statsMutex);

    ++m_frames;
    m_drawCalls += drawCalls;
}

float CStatsManager::ComputeCpuUsageRatio(
    int32 idleTicks,
    int32 totalTicks)
{
    idleTicks = std::max<int32>(idleTicks, 0);

    if(totalTicks <= 0)
    {
        return 0.f;
    }

    float idleRatio =
        static_cast<float>(idleTicks) /
        static_cast<float>(totalTicks);

    if(idleRatio < 0.f)
    {
        idleRatio = 0.f;
    }
    else if(idleRatio > 1.f)
    {
        idleRatio = 1.f;
    }

    return (1.f - idleRatio) * 100.f;
}

uint32 CStatsManager::GetFrames()
{
    std::lock_guard<std::mutex> statsLock(m_statsMutex);
    return m_frames;
}

uint32 CStatsManager::GetDrawCalls()
{
    std::lock_guard<std::mutex> statsLock(m_statsMutex);
    return m_drawCalls;
}

CPS2VM::CPU_UTILISATION_INFO CStatsManager::GetCpuUtilisationInfo()
{
    std::lock_guard<std::mutex> statsLock(m_statsMutex);
    return m_cpuUtilisation;
}

#ifdef PROFILE

std::string CStatsManager::GetProfilingInfo()
{
    // Keep lock order consistent:
    // stats mutex first, profiler mutex second.
    std::lock_guard<std::mutex> statsLock(m_statsMutex);
    std::lock_guard<std::mutex> profileLock(m_profilerZonesMutex);

    const uint32 frameCount = m_frames;
    const auto cpuUtilisation = m_cpuUtilisation;

    std::string result;
    uint64 totalTime = 0;

    for(const auto& zonePair : m_profilerZones)
    {
        totalTime += zonePair.second.currentValue;
    }

    // Profiler timestamps are measured in nanoseconds.
    static const uint64 timeScale = 1000000;

    for(const auto& zonePair : m_profilerZones)
    {
        const auto& zoneInfo = zonePair.second;

        const double avgRatioSpent =
            totalTime != 0
                ? static_cast<double>(zoneInfo.currentValue) /
                  static_cast<double>(totalTime)
                : 0.0;

        const double avgMsSpent =
            frameCount != 0
                ? static_cast<double>(zoneInfo.currentValue) /
                  (static_cast<double>(frameCount) * timeScale)
                : 0.0;

        const double minMsSpent =
            zoneInfo.minValue != ~0ULL
                ? static_cast<double>(zoneInfo.minValue) /
                  static_cast<double>(timeScale)
                : 0.0;

        const double maxMsSpent =
            static_cast<double>(zoneInfo.maxValue) /
            static_cast<double>(timeScale);

        result += string_format(
            "%10s %6.2f%% %6.2fms %6.2fms %6.2fms\r\n",
            zonePair.first.c_str(),
            avgRatioSpent * 100.0,
            avgMsSpent,
            minMsSpent,
            maxMsSpent);
    }

    if(!m_profilerZones.empty())
    {
        const double totalAvgMsSpent =
            frameCount != 0
                ? static_cast<double>(totalTime) /
                  (static_cast<double>(frameCount) * timeScale)
                : 0.0;

        result += string_format(
            "                   %6.2fms\r\n\r\n",
            totalAvgMsSpent);
    }

    const float eeUsageRatio = ComputeCpuUsageRatio(
        cpuUtilisation.eeIdleTicks,
        cpuUtilisation.eeTotalTicks);

    const float iopUsageRatio = ComputeCpuUsageRatio(
        cpuUtilisation.iopIdleTicks,
        cpuUtilisation.iopTotalTicks);

    result += string_format(
        "EE Usage:  %6.2f%%\r\n",
        eeUsageRatio);

    result += string_format(
        "IOP Usage: %6.2f%%\r\n",
        iopUsageRatio);

    return result;
}

#endif

std::string CStatsManager::GetRuntimeExecutionDiagnostics() const
{
    std::string result;

    const auto formatPc = [](uint32 pc) -> std::string
    {
        if(pc == 0xFFFFFFFF)
        {
            return "UNAVAILABLE";
        }

        return string_format("0x%08X", pc);
    };

    result += "EE EXECUTION\n";
    result += "------------------------------------\n";

    result += string_format(
        "Execution ticks: %llu\n"
        "Execution batches: %llu\n"
        "Instruction count: UNAVAILABLE\n"
        "First observed PC: %s\n"
        "Last observed PC: %s\n"
        "Idle ticks: %llu\n"
        "Last idle state: %s\n\n",
        static_cast<unsigned long long>(m_eeExecutionTicks.load()),
        static_cast<unsigned long long>(m_eeExecutionBatches.load()),
        formatPc(m_eeFirstPc.load()).c_str(),
        formatPc(m_eeLastPc.load()).c_str(),
        static_cast<unsigned long long>(m_eeIdleTicks.load()),
        m_eeExecutionBatches.load() == 0
            ? "UNAVAILABLE"
            : (m_eeLastIdle.load() ? "IDLE" : "ACTIVE"));

    result += "IOP EXECUTION\n";
    result += "------------------------------------\n";

    result += string_format(
        "Execution ticks: %llu\n"
        "Execution batches: %llu\n"
        "Instruction count: UNAVAILABLE\n"
        "First observed PC: %s\n"
        "Last observed PC: %s\n"
        "Idle ticks: %llu\n"
        "Last idle state: %s\n\n",
        static_cast<unsigned long long>(m_iopExecutionTicks.load()),
        static_cast<unsigned long long>(m_iopExecutionBatches.load()),
        formatPc(m_iopFirstPc.load()).c_str(),
        formatPc(m_iopLastPc.load()).c_str(),
        static_cast<unsigned long long>(m_iopIdleTicks.load()),
        m_iopExecutionBatches.load() == 0
            ? "UNAVAILABLE"
            : (m_iopLastIdle.load() ? "IDLE" : "ACTIVE"));

    static const char* cdvdNames[3] = {
        "EE READ",
        "IOP READ",
        "STREAM READ"
    };

    result += "CDVD READ DIAGNOSTICS\n";
    result += "------------------------------------\n";

    for(uint32 i = 0; i < 3; i++)
    {
        result += string_format(
            "%s\n"
            "Requests: %llu\n"
            "First sector: %s\n"
            "Last request start sector: %u\n"
            "Requested sectors: %llu\n"
            "Successful bytes: %llu\n"
            "Failures: %llu\n"
            "Rejected requests: %llu\n\n",
            cdvdNames[i],
            static_cast<unsigned long long>(m_cdvdRequests[i].load()),
            formatPc(m_cdvdFirstSector[i].load()).c_str(),
            m_cdvdLastSector[i].load(),
            static_cast<unsigned long long>(m_cdvdRequestedSectors[i].load()),
            static_cast<unsigned long long>(m_cdvdSuccessfulBytes[i].load()),
            static_cast<unsigned long long>(m_cdvdFailures[i].load()),
            static_cast<unsigned long long>(m_cdvdRejected[i].load()));
    }

    static const char* primitiveNames[7] = {
        "POINT",
        "LINE",
        "LINESTRIP",
        "TRIANGLE",
        "TRIANGLESTRIP",
        "TRIANGLEFAN",
        "SPRITE"
    };

    result += "GS RENDERING DIAGNOSTICS\n";
    result += "------------------------------------\n";

    for(uint32 i = 0; i < 7; i++)
    {
        result += string_format(
            "%s: %llu primitives, %llu vertices\n",
            primitiveNames[i],
            static_cast<unsigned long long>(m_gsPrimitiveCounts[i].load()),
            static_cast<unsigned long long>(m_gsPrimitiveVertices[i].load()));
    }

    result += string_format(
        "\nGS empty buffer events: %llu\n"
        "GS draw-disabled events: %llu\n"
        "GS buffer flushes: %llu\n"
        "GS draw submissions: %llu\n"
        "GS vertex kicks: %llu\n"
        "GS drawing kicks: %llu\n"
        "GS vertices buffered: %llu\n"
        "GS register writes: %llu\n",
        static_cast<unsigned long long>(m_gsEmptyBufferEvents.load()),
        static_cast<unsigned long long>(m_gsDrawDisabledEvents.load()),
        static_cast<unsigned long long>(m_gsBufferFlushes.load()),
        static_cast<unsigned long long>(m_gsDrawSubmissions.load()),
        static_cast<unsigned long long>(m_gsVertexKicks.load()),
        static_cast<unsigned long long>(m_gsDrawingKicks.load()),
        static_cast<unsigned long long>(m_gsBufferedVertices.load()),
        static_cast<unsigned long long>(m_gsRegisterWrites.load()));

    return result;
}

void CStatsManager::ClearStats()
{
    std::lock_guard<std::mutex> statsLock(m_statsMutex);

    m_frames = 0;
    m_drawCalls = 0;
    m_cpuUtilisation = CPS2VM::CPU_UTILISATION_INFO();

    // Reset existing GS counters.
    m_gsRegisterWrites.store(0);
    m_gsVertexKicks.store(0);
    m_gsDrawingKicks.store(0);
    m_gsBufferedVertices.store(0);
    m_gsBufferFlushes.store(0);
    m_gsDrawSubmissions.store(0);

    // Reset EE diagnostics.
    m_eeExecutionTicks.store(0);
    m_eeExecutionBatches.store(0);
    m_eeIdleTicks.store(0);
    m_eeFirstPc.store(0xFFFFFFFF);
    m_eeLastPc.store(0);
    m_eeLastIdle.store(0);

    // Reset IOP diagnostics.
    m_iopExecutionTicks.store(0);
    m_iopExecutionBatches.store(0);
    m_iopIdleTicks.store(0);
    m_iopFirstPc.store(0xFFFFFFFF);
    m_iopLastPc.store(0);
    m_iopLastIdle.store(0);

    // Reset CDVD diagnostics.
    for(uint32 i = 0; i < 3; i++)
    {
        m_cdvdRequests[i].store(0);
        m_cdvdRequestedSectors[i].store(0);
        m_cdvdSuccessfulBytes[i].store(0);
        m_cdvdFailures[i].store(0);
        m_cdvdRejected[i].store(0);
        m_cdvdFirstSector[i].store(0xFFFFFFFF);
        m_cdvdLastSector[i].store(0);
    }

    // Reset GS primitive diagnostics.
    for(uint32 i = 0; i < 7; i++)
    {
        m_gsPrimitiveCounts[i].store(0);
        m_gsPrimitiveVertices[i].store(0);
    }

    m_gsEmptyBufferEvents.store(0);
    m_gsDrawDisabledEvents.store(0);
    
#ifdef PROFILE
    std::lock_guard<std::mutex> profileLock(m_profilerZonesMutex);

    for(auto& zonePair : m_profilerZones)
    {
        zonePair.second.currentValue = 0;
        zonePair.second.minValue = ~0ULL;
        zonePair.second.maxValue = 0;
    }
#endif
}

void CStatsManager::RecordGifFifoWrite()
{
    m_gifFifoWrites.fetch_add(1);
}

void CStatsManager::RecordGifPacket()
{
    m_gifPackets.fetch_add(1);
}

void CStatsManager::RecordGifDmaQwc(uint32 qwc)
{
    m_gifDmaQwc.fetch_add(qwc);
}

void CStatsManager::RecordGifFifoStall()
{
    m_gifFifoStalls.fetch_add(1);
}
