#include <cassert>
#include <chrono>
#include <cstring>
#include <utility>

#include "SH_OpenSL.h"

CSH_OpenSL::CSH_OpenSL()
{
	SLresult result = SL_RESULT_SUCCESS;

	result = slCreateEngine(&m_engineObject, 0, nullptr, 0, nullptr, nullptr);
	assert(result == SL_RESULT_SUCCESS);
	if(result != SL_RESULT_SUCCESS) return;

	result = (*m_engineObject)->Realize(m_engineObject, SL_BOOLEAN_FALSE);
	assert(result == SL_RESULT_SUCCESS);
	if(result != SL_RESULT_SUCCESS) return;

	result = (*m_engineObject)->GetInterface(
	    m_engineObject, SL_IID_ENGINE, &m_engine);
	assert(result == SL_RESULT_SUCCESS);
	if(result != SL_RESULT_SUCCESS) return;

	CreateOutputMix();
	CreateAudioPlayer();
}

CSH_OpenSL::~CSH_OpenSL()
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_isShuttingDown = true;
		m_isResetting = true;
	}

	// Wake a Write() call if it is waiting for an available slot.
	m_bufferAvailable.notify_all();

	// Stop queued playback before destroying the OpenSL objects.
	if(m_playerQueue != nullptr)
	{
		SLresult result = (*m_playerQueue)->Clear(m_playerQueue);
		assert(result == SL_RESULT_SUCCESS);
	}

	if(m_playerObject != nullptr)
	{
		(*m_playerObject)->Destroy(m_playerObject);
		m_playerObject = nullptr;
	}

	m_playerPlay = nullptr;
	m_playerQueue = nullptr;

	if(m_outputMixObject != nullptr)
	{
		(*m_outputMixObject)->Destroy(m_outputMixObject);
		m_outputMixObject = nullptr;
	}

	if(m_engineObject != nullptr)
	{
		(*m_engineObject)->Destroy(m_engineObject);
		m_engineObject = nullptr;
	}

	m_engine = nullptr;
}

CSoundHandler* CSH_OpenSL::HandlerFactory()
{
	return new CSH_OpenSL();
}

void CSH_OpenSL::CreateOutputMix()
{
	assert(m_outputMixObject == nullptr);

	SLresult result = SL_RESULT_SUCCESS;

	static const SLInterfaceID interfaceIds[] = {SL_IID_VOLUME};
	static const SLboolean required[] = {SL_BOOLEAN_FALSE};

	result = (*m_engine)->CreateOutputMix(
	    m_engine,
	    &m_outputMixObject,
	    1,
	    interfaceIds,
	    required);

	assert(result == SL_RESULT_SUCCESS);
	if(result != SL_RESULT_SUCCESS) return;

	result = (*m_outputMixObject)->Realize(
	    m_outputMixObject,
	    SL_BOOLEAN_FALSE);

	assert(result == SL_RESULT_SUCCESS);
}

void CSH_OpenSL::CreateAudioPlayer()
{
	assert(m_playerObject == nullptr);

	SLresult result = SL_RESULT_SUCCESS;

	SLDataLocator_AndroidSimpleBufferQueue bufferQueueLocator = {};
	bufferQueueLocator.locatorType =
	    SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE;
	bufferQueueLocator.numBuffers = BUFFER_COUNT;

	SLDataFormat_PCM dataFormat = {};
	dataFormat.formatType = SL_DATAFORMAT_PCM;
	dataFormat.numChannels = 2;
	dataFormat.samplesPerSec = SL_SAMPLINGRATE_44_1;
	dataFormat.bitsPerSample = SL_PCMSAMPLEFORMAT_FIXED_16;
	dataFormat.containerSize = SL_PCMSAMPLEFORMAT_FIXED_16;
	dataFormat.channelMask =
	    SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT;
	dataFormat.endianness = SL_BYTEORDER_LITTLEENDIAN;

	SLDataSource dataSource = {};
	dataSource.pLocator = &bufferQueueLocator;
	dataSource.pFormat = &dataFormat;

	SLDataLocator_OutputMix outputMixLocator = {};
	outputMixLocator.locatorType = SL_DATALOCATOR_OUTPUTMIX;
	outputMixLocator.outputMix = m_outputMixObject;

	SLDataSink dataSink = {};
	dataSink.pLocator = &outputMixLocator;

	static const SLInterfaceID interfaceIds[] =
	    {SL_IID_ANDROIDSIMPLEBUFFERQUEUE};

	static const SLboolean required[] = {SL_BOOLEAN_TRUE};

	result = (*m_engine)->CreateAudioPlayer(
	    m_engine,
	    &m_playerObject,
	    &dataSource,
	    &dataSink,
	    1,
	    interfaceIds,
	    required);

	assert(result == SL_RESULT_SUCCESS);
	if(result != SL_RESULT_SUCCESS) return;

	result = (*m_playerObject)->Realize(
	    m_playerObject,
	    SL_BOOLEAN_FALSE);

	assert(result == SL_RESULT_SUCCESS);
	if(result != SL_RESULT_SUCCESS) return;

	result = (*m_playerObject)->GetInterface(
	    m_playerObject,
	    SL_IID_PLAY,
	    &m_playerPlay);

	assert(result == SL_RESULT_SUCCESS);
	if(result != SL_RESULT_SUCCESS) return;

	result = (*m_playerObject)->GetInterface(
	    m_playerObject,
	    SL_IID_ANDROIDSIMPLEBUFFERQUEUE,
	    &m_playerQueue);

	assert(result == SL_RESULT_SUCCESS);
	if(result != SL_RESULT_SUCCESS) return;

	result = (*m_playerQueue)->RegisterCallback(
	    m_playerQueue,
	    &CSH_OpenSL::QueueCallback,
	    this);

	assert(result == SL_RESULT_SUCCESS);
	if(result != SL_RESULT_SUCCESS) return;

	result = (*m_playerPlay)->SetPlayState(
	    m_playerPlay,
	    SL_PLAYSTATE_PLAYING);

	assert(result == SL_RESULT_SUCCESS);
}

void CSH_OpenSL::QueueCallback(
    SLAndroidSimpleBufferQueueItf,
    void* context)
{
	auto* handler = reinterpret_cast<CSH_OpenSL*>(context);
	if(handler == nullptr) return;

	handler->QueueCallbackImpl();
}

void CSH_OpenSL::QueueCallbackImpl()
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		if(m_isShuttingDown || m_isResetting)
		{
			return;
		}

		// A callback means the oldest queued slot has finished playback.
		if(m_queuedSlots.empty())
		{
			return;
		}

		const int completedSlot = m_queuedSlots.front();
		m_queuedSlots.pop_front();

		m_bufferInUse[completedSlot] = false;

		// The final queued buffer has completed. The output queue
		// has drained and may experience an underrun until refilled.
		if(m_queuedSlots.empty())
		{
			m_underrunCount.fetch_add(1, std::memory_order_relaxed);
		}
	}

	// Wake the emulation thread if it is waiting for a free slot.
	m_bufferAvailable.notify_one();
}

int CSH_OpenSL::FindFreeBufferLocked() const
{
	for(int i = 0; i < BUFFER_COUNT; i++)
	{
		if(!m_bufferInUse[i])
		{
			return i;
		}
	}

	return -1;
}

void CSH_OpenSL::Reset()
{
	assert(m_playerQueue != nullptr);
	if(m_playerQueue == nullptr) return;

	{
		std::lock_guard<std::mutex> lock(m_mutex);

		if(m_isShuttingDown)
		{
			return;
		}

		m_isResetting = true;
	}

	// Clear the OpenSL queue without holding the mutex:
	// callbacks must be able to finish without deadlocking Clear().
	SLresult result = (*m_playerQueue)->Clear(m_playerQueue);
	assert(result == SL_RESULT_SUCCESS);

	{
		std::lock_guard<std::mutex> lock(m_mutex);

		m_queuedSlots.clear();

		for(auto& slot : m_bufferInUse)
		{
			slot = false;
		}

		m_isResetting = false;
	}

	m_bufferAvailable.notify_all();
}

void CSH_OpenSL::Write(
    int16* buffer,
    unsigned int sampleCount,
    unsigned int sampleRate)
{
	// The PS2 SPU path currently produces 44.1 kHz audio.
	// OpenSL output format remains 44.1 kHz in this patch.
	(void)sampleRate;

	if(buffer == nullptr || sampleCount == 0)
	{
		return;
	}

	if(m_playerQueue == nullptr)
	{
		return;
	}

	using Clock = std::chrono::steady_clock;

	const auto waitStart = Clock::now();
	bool hadToWait = false;
	int slot = -1;

	{
		std::unique_lock<std::mutex> lock(m_mutex);

		slot = FindFreeBufferLocked();

		if(slot < 0)
		{
			hadToWait = true;

			m_backpressureWaitCount.fetch_add(
			    1,
			    std::memory_order_relaxed);

			m_bufferAvailable.wait(
			    lock,
			    [this]() {
				    return m_isShuttingDown ||
				           (!m_isResetting &&
				            FindFreeBufferLocked() >= 0);
			    });

			if(m_isShuttingDown)
			{
				return;
			}

			slot = FindFreeBufferLocked();
		}

		if(slot < 0 || m_isShuttingDown)
		{
			return;
		}

		// Reserve this slot before releasing the mutex.
		m_bufferInUse[slot] = true;
	}

	if(hadToWait)
	{
		const auto waited =
		    std::chrono::duration_cast<std::chrono::nanoseconds>(
		        Clock::now() - waitStart);

		m_backpressureWaitNanoseconds.fetch_add(
		    static_cast<uint64_t>(waited.count()),
		    std::memory_order_relaxed);
	}

	// Copy the samples into memory owned by this queue slot.
	// The source buffer can now be reused by CPS2VM safely.
	try
	{
		m_buffers[slot].assign(buffer, buffer + sampleCount);
	}
	catch(...)
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_bufferInUse[slot] = false;
		}

		m_bufferAvailable.notify_one();
		throw;
	}

	SLresult result = SL_RESULT_SUCCESS;

	{
		std::lock_guard<std::mutex> lock(m_mutex);

		if(m_isShuttingDown || m_isResetting)
		{
			m_bufferInUse[slot] = false;
			m_bufferAvailable.notify_one();
			return;
		}

		// Record the slot before Enqueue. The callback takes this
		// same mutex, so it cannot consume the slot prematurely.
		m_queuedSlots.push_back(slot);

		result = (*m_playerQueue)->Enqueue(
		    m_playerQueue,
		    m_buffers[slot].data(),
		    static_cast<SLuint32>(
		        m_buffers[slot].size() * sizeof(int16)));

		if(result != SL_RESULT_SUCCESS)
		{
			// Enqueue failed: return the reserved slot.
			if(!m_queuedSlots.empty() &&
			   m_queuedSlots.back() == slot)
			{
				m_queuedSlots.pop_back();
			}

			m_bufferInUse[slot] = false;
		}
	}

	if(result != SL_RESULT_SUCCESS)
	{
		m_bufferAvailable.notify_one();
		assert(result == SL_RESULT_SUCCESS);
		return;
	}
}

bool CSH_OpenSL::HasFreeBuffers()
{
	std::lock_guard<std::mutex> lock(m_mutex);

	if(m_isShuttingDown || m_isResetting)
	{
		return false;
	}

	return FindFreeBufferLocked() >= 0;
}

void CSH_OpenSL::RecycleBuffers()
{
	// OpenSL returns each slot through QueueCallbackImpl().
	// Do not reset the slot count here: that would incorrectly
	// mark buffers still being played as available.
}

uint64_t CSH_OpenSL::GetUnderrunCount() const
{
	return m_underrunCount.load(std::memory_order_relaxed);
}

uint64_t CSH_OpenSL::GetBackpressureWaitCount() const
{
	return m_backpressureWaitCount.load(std::memory_order_relaxed);
}

uint64_t CSH_OpenSL::GetBackpressureWaitNanoseconds() const
{
	return m_backpressureWaitNanoseconds.load(std::memory_order_relaxed);
}
