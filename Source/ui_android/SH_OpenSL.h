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
	~CSH_OpenSL() override;

	static CSoundHandler* HandlerFactory();

	void Reset() override;
	void Write(int16*, unsigned int, unsigned int) override;
	bool HasFreeBuffers() override;
	void RecycleBuffers() override;

	// Diagnostic counters. These are not proof of audible dropouts.
	uint64_t GetUnderrunCount() const;
	uint64_t GetBackpressureWaitCount() const;
	uint64_t GetBackpressureWaitNanoseconds() const;

    unsigned int GetQueuedBufferCount() const;
    unsigned int GetBufferCapacity() const;

private:
	enum
	{
		BUFFER_COUNT = 5,
	};

	void CreateOutputMix();
	void CreateAudioPlayer();
	void DestroyOpenSLObjects();

	static void QueueCallback(
	    SLAndroidSimpleBufferQueueItf,
	    void*);

	void QueueCallbackImpl();

	int FindFreeBufferLocked() const;

	SLObjectItf m_engineObject = nullptr;
	SLEngineItf m_engine = nullptr;

	SLObjectItf m_outputMixObject = nullptr;

	SLObjectItf m_playerObject = nullptr;
	SLPlayItf m_playerPlay = nullptr;
	SLAndroidSimpleBufferQueueItf m_playerQueue = nullptr;

	// Each queued OpenSL buffer owns its own sample memory.
	std::array<std::vector<int16>, BUFFER_COUNT> m_buffers;

	// A slot remains occupied until its queue callback completes.
	std::array<bool, BUFFER_COUNT> m_bufferInUse = {};

	// OpenSL simple buffer queue callbacks complete in FIFO order.
	std::deque<int> m_queuedSlots;

	// Serializes complete Write and Reset operations.
	// The callback does not acquire this mutex.
	std::mutex m_operationMutex;

	// Protects queue bookkeeping and lifecycle state.
	mutable std::mutex m_mutex;
	std::condition_variable m_bufferAvailable;
	std::condition_variable m_callbackIdle;

	bool m_isResetting = false;
	bool m_isShuttingDown = false;

	// Number of callbacks currently executing QueueCallbackImpl().
	unsigned int m_activeCallbacks = 0;

	std::atomic<uint64_t> m_underrunCount{0};
	std::atomic<uint64_t> m_backpressureWaitCount{0};
	std::atomic<uint64_t> m_backpressureWaitNanoseconds{0};
};
