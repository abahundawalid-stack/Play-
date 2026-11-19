#pragma once

#include <atomic>
#include <string>
#include <mutex>
#include <map>
#include "Types.h"
#include "Singleton.h"
#include "../Profiler.h"
#include "../PS2VM.h"

class CStatsManager : public CSingleton<CStatsManager>
{
public:
	void OnNewFrame(CPS2VM*);
	void OnGsNewFrame(uint32);

    void RecordGsRegisterWrite();
    void RecordGsVertexKick(bool drawingKick);
    void RecordGsBufferedVertices(uint32 count);
    void RecordGsBufferFlush();
    void RecordGsDrawSubmission();

    std::string GetGsPipelineDiagnostics() const;

    // Runtime CPU diagnostics.
    void RecordCpuExecution(
        bool isEe,
        uint64 ticks,
        uint32 pc,
        bool idle);

    // CDVD diagnostics:
    // type: 0 = EE, 1 = IOP, 2 = STREAM
    void RecordCdvdRequest(
        uint32 type,
        uint32 firstSector,
        uint32 sectorCount);

    void RecordCdvdReadSuccess(
        uint32 type,
        uint64 bytes);

    void RecordCdvdFailure(
        uint32 type,
        bool rejected);

    // GS diagnostics:
    // primitiveType uses CGSHandler::PRIM_TYPE values.
    void RecordGsPrimitive(
        uint32 primitiveType,
        uint32 vertexCount);

    void RecordGsEmptyBuffer();
    void RecordGsRenderingSkipped(bool drawDisabled);

    std::string GetRuntimeExecutionDiagnostics() const;

	static float ComputeCpuUsageRatio(int32 idleTicks, int32 totalTicks);

	uint32 GetFrames();
	uint32 GetDrawCalls();
	CPS2VM::CPU_UTILISATION_INFO GetCpuUtilisationInfo();
#ifdef PROFILE
	std::string GetProfilingInfo();
#endif

	void ClearStats();

private:
	std::mutex m_statsMutex;

	uint32 m_frames = 0;
	uint32 m_drawCalls = 0;

    std::atomic<uint64> m_gsRegisterWrites{0};
    std::atomic<uint64> m_gsVertexKicks{0};
    std::atomic<uint64> m_gsDrawingKicks{0};
    std::atomic<uint64> m_gsBufferedVertices{0};
    std::atomic<uint64> m_gsBufferFlushes{0};
    std::atomic<uint64> m_gsDrawSubmissions{0};

	CPS2VM::CPU_UTILISATION_INFO m_cpuUtilisation;

#ifdef PROFILE
	struct ZONEINFO
	{
		uint64 currentValue = 0;
		uint64 minValue = ~0ULL;
		uint64 maxValue = 0;
	};

	typedef std::map<std::string, ZONEINFO> ZoneMap;

	std::mutex m_profilerZonesMutex;
	ZoneMap m_profilerZones;
#endif
};
