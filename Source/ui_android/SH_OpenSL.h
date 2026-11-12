#pragma once

#include "sound/SoundHandler.h"

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

class CSH_OpenSL : public CSoundHandler
{
public:
	CSH_OpenSL();
	virtual ~CSH_OpenSL();

	static CSoundHandler* HandlerFactory();

	void Reset() override;
	void Write(int16*, unsigned int, unsigned int) override;
	bool HasFreeBuffers() override;
	void RecycleBuffers() override;

	// Telemetry. These counters are diagnostic only.
	uint64_t GetUnderrunCount() const;
	uint64_t GetBackpressureWaitCount() const;
	uint64_t GetBackpressureWaitNanoseconds() const;

private:
	enum
	{
		BUFFER_COUNT = 5,
	};

	void CreateOutputMix();
	void CreateAudioPlayer();

	static void QueueCallback(SLAndroidSimpleBufferQueueItf, void*);
	void QueueCallbackImpl();

	int FindFreeBufferLocked() const;

	SLObjectItf m_engineObject = nullptr;
	SLEngineItf m_engine = nullptr;

	SLObjectItf m_outputMixObject = nullptr;

	SLObjectItf m_playerObject = nullptr;
	SLPlayItf m_playerPlay = nullptr;
	SLAndroidSimpleBufferQueueItf m_playerQueue = nullptr;

	// Each queued OpenSL buffer owns its own memory.
	std::array<std::vector<int16>, BUFFER_COUNT> m_buffers;

	// A slot remains occupied until its OpenSL callback arrives.
	std::array<bool, BUFFER_COUNT> m_bufferInUse = {};

	// OpenSL callbacks complete buffers in FIFO order.
	std::deque<int> m_queuedSlots;

	mutable std::mutex m_mutex;
	std::condition_variable m_bufferAvailable;

	bool m_isResetting = false;
	bool m_isShuttingDown = false;

	std::atomic<uint64_t> m_underrunCount{0};
	std::atomic<uint64_t> m_backpressureWaitCount{0};
	std::atomic<uint64_t> m_backpressureWaitNanoseconds{0};
};
