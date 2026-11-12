#include <cassert>
#include <chrono>
#include <limits>
#include <stdexcept>

#include "SH_OpenSL.h"

CSH_OpenSL::CSH_OpenSL()
{
	try
	{
		SLresult result = slCreateEngine(
		    &m_engineObject,
		    0,
		    nullptr,
		    0,
		    nullptr,
		    nullptr);

		if(result != SL_RESULT_SUCCESS)
		{
			throw std::runtime_error("OpenSL: slCreateEngine failed.");
		}

		result = (*m_engineObject)->Realize(
		    m_engineObject,
		    SL_BOOLEAN_FALSE);

		if(result != SL_RESULT_SUCCESS)
		{
			throw std::runtime_error("OpenSL: engine Realize failed.");
		}

		result = (*m_engineObject)->GetInterface(
		    m_engineObject,
		    SL_IID_ENGINE,
		    &m_engine);

		if(result != SL_RESULT_SUCCESS)
		{
			throw std::runtime_error("OpenSL: engine interface failed.");
		}

		CreateOutputMix();
		CreateAudioPlayer();
	}
	catch(...)
	{
		DestroyOpenSLObjects();
		throw;
	}
}

CSH_OpenSL::~CSH_OpenSL()
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_isShuttingDown = true;
		m_isResetting = true;
	}

	// Release a Write() call waiting for a free buffer.
	m_bufferAvailable.notify_all();

	// Wait until any active Write() or Reset() operation exits.
	std::lock_guard<std::mutex> operationLock(m_operationMutex);

	DestroyOpenSLObjects();
}

CSoundHandler* CSH_OpenSL::HandlerFactory()
{
	try
	{
		return new CSH_OpenSL();
	}
	catch(...)
	{
		return nullptr;
	}
}

void CSH_OpenSL::CreateOutputMix()
{
	if(m_engine == nullptr)
	{
		throw std::runtime_error("OpenSL: engine is unavailable.");
	}

	assert(m_outputMixObject == nullptr);

	static const SLInterfaceID interfaceIds[] =
	    {SL_IID_VOLUME};

	static const SLboolean required[] =
	    {SL_BOOLEAN_FALSE};

	SLresult result = (*m_engine)->CreateOutputMix(
	    m_engine,
	    &m_outputMixObject,
	    1,
	    interfaceIds,
	    required);

	if(result != SL_RESULT_SUCCESS)
	{
		throw std::runtime_error("OpenSL: CreateOutputMix failed.");
	}

	result = (*m_outputMixObject)->Realize(
	    m_outputMixObject,
	    SL_BOOLEAN_FALSE);

	if(result != SL_RESULT_SUCCESS)
	{
		throw std::runtime_error("OpenSL: output mix Realize failed.");
	}
}

void CSH_OpenSL::CreateAudioPlayer()
{
	if(m_engine == nullptr || m_outputMixObject == nullptr)
	{
		throw std::runtime_error("OpenSL: output mix is unavailable.");
	}

	assert(m_playerObject == nullptr);

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
	dataSink.pFormat = nullptr;

	static const SLInterfaceID interfaceIds[] =
	    {SL_IID_ANDROIDSIMPLEBUFFERQUEUE};

	static const SLboolean required[] =
	    {SL_BOOLEAN_TRUE};

	SLresult result = (*m_engine)->CreateAudioPlayer(
	    m_engine,
	    &m_playerObject,
	    &dataSource,
	    &dataSink,
	    1,
	    interfaceIds,
	    required);

	if(result != SL_RESULT_SUCCESS)
	{
		throw std::runtime_error("OpenSL: CreateAudioPlayer failed.");
	}

	result = (*m_playerObject)->Realize(
	    m_playerObject,
	    SL_BOOLEAN_FALSE);

	if(result != SL_RESULT_SUCCESS)
	{
		throw std::runtime_error("OpenSL: player Realize failed.");
	}

	result = (*m_playerObject)->GetInterface(
	    m_playerObject,
	    SL_IID_PLAY,
	    &m_playerPlay);

	if(result != SL_RESULT_SUCCESS)
	{
		throw std::runtime_error("OpenSL: player interface failed.");
	}

	result = (*m_playerObject)->GetInterface(
	    m_playerObject,
	    SL_IID_ANDROIDSIMPLEBUFFERQUEUE,
	    &m_playerQueue);

	if(result != SL_RESULT_SUCCESS)
	{
		throw std::runtime_error("OpenSL: buffer queue interface failed.");
	}

	result = (*m_playerQueue)->RegisterCallback(
	    m_playerQueue,
	    &CSH_OpenSL::QueueCallback,
	    this);

	if(result != SL_RESULT_SUCCESS)
	{
		throw std::runtime_error("OpenSL: RegisterCallback failed.");
	}

	result = (*m_playerPlay)->SetPlayState(
	    m_playerPlay,
	    SL_PLAYSTATE_PLAYING);

	if(result != SL_RESULT_SUCCESS)
	{
		throw std::runtime_error("OpenSL: SetPlayState failed.");
	}
}

void CSH_OpenSL::DestroyOpenSLObjects()
{
	// This function is called only after new writes have been blocked
	// and the operation mutex has excluded active Write/Reset calls.

	if(m_playerPlay != nullptr)
	{
		(*m_playerPlay)->SetPlayState(
		    m_playerPlay,
		    SL_PLAYSTATE_STOPPED);
	}

	if(m_playerQueue != nullptr)
	{
		(*m_playerQueue)->Clear(m_playerQueue);
	}

	if(m_playerObject != nullptr)
	{
		// Destroy the player before allowing this object to disappear.
		// OpenSL object destruction is expected to stop further queue
		// callbacks; callbacks already executing are waited for below.
		(*m_playerObject)->Destroy(m_playerObject);
		m_playerObject = nullptr;
	}

	m_playerPlay = nullptr;
	m_playerQueue = nullptr;

	{
		std::unique_lock<std::mutex> lock(m_mutex);

		m_callbackIdle.wait(
		    lock,
		    [this]() {
			    return m_activeCallbacks == 0;
		    });
	}

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
	bool notifyWriter = false;

	{
		std::lock_guard<std::mutex> lock(m_mutex);

		++m_activeCallbacks;

		if(!m_isShuttingDown && !m_isResetting)
		{
			if(!m_queuedSlots.empty())
			{
				const int completedSlot = m_queuedSlots.front();
				m_queuedSlots.pop_front();

				if(completedSlot >= 0 &&
				   completedSlot < BUFFER_COUNT)
				{
					m_bufferInUse[completedSlot] = false;
					notifyWriter = true;
				}

				if(m_queuedSlots.empty())
				{
					m_underrunCount.fetch_add(
					    1,
					    std::memory_order_relaxed);
				}
			}
		}
	}

	// Beri tahu penulis sebelum callback dinyatakan selesai.
	if(notifyWriter)
	{
		m_bufferAvailable.notify_one();
	}

	{
		std::lock_guard<std::mutex> lock(m_mutex);

		--m_activeCallbacks;

		if(m_activeCallbacks == 0)
		{
			m_callbackIdle.notify_all();
		}
	}
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
	// Reset and Write are serialized for their entire operation.
	std::lock_guard<std::mutex> operationLock(m_operationMutex);

	if(m_playerQueue == nullptr)
	{
		return;
	}

	{
		std::lock_guard<std::mutex> lock(m_mutex);

		if(m_isShuttingDown)
		{
			return;
		}

		m_isResetting = true;
	}

	// Do not hold m_mutex while calling Clear:
	// OpenSL may need to complete a callback during this operation.
	SLresult result = (*m_playerQueue)->Clear(m_playerQueue);

	if(result != SL_RESULT_SUCCESS)
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_isResetting = false;
		}

		m_bufferAvailable.notify_all();
		return;
	}

	{
		std::lock_guard<std::mutex> lock(m_mutex);

		m_queuedSlots.clear();

		for(auto& inUse : m_bufferInUse)
		{
			inUse = false;
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
	// Current SPU output and OpenSL PCM format are both 44.1 kHz.
	// No resampling or time stretching is performed here.
	(void)sampleRate;

	if(buffer == nullptr || sampleCount == 0)
	{
		return;
	}

	if(m_playerQueue == nullptr)
	{
		return;
	}

	if(sampleCount >
	   std::numeric_limits<SLuint32>::max() / sizeof(int16))
	{
		return;
	}

	using Clock = std::chrono::steady_clock;

	const auto waitStart = Clock::now();
	bool hadToWait = false;

	// This lock prevents Reset() from clearing or reusing a slot
	// between reservation, sample copy and Enqueue().
	std::lock_guard<std::mutex> operationLock(m_operationMutex);

	int slot = -1;

	{
		std::unique_lock<std::mutex> lock(m_mutex);

		if(m_isShuttingDown)
		{
			return;
		}

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

		if(slot < 0)
		{
			return;
		}

		// Reserve the slot before releasing the state mutex.
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

	// The operation mutex remains held, so Reset cannot reuse this slot.
	try
	{
		m_buffers[slot].assign(
		    buffer,
		    buffer + sampleCount);
	}
	catch(...)
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_bufferInUse[slot] = false;
		}

		m_bufferAvailable.notify_one();
		return;
	}

	const SLuint32 bufferSize = static_cast<SLuint32>(
	    m_buffers[slot].size() * sizeof(int16));

	SLresult result = SL_RESULT_SUCCESS;

	{
		std::lock_guard<std::mutex> lock(m_mutex);

		if(m_isShuttingDown || m_isResetting)
		{
			m_bufferInUse[slot] = false;
			m_bufferAvailable.notify_one();
			return;
		}

		// Queue bookkeeping is recorded before Enqueue.
		// The callback uses this same mutex and cannot consume
		// this entry until Enqueue has returned.
		try
		{
			m_queuedSlots.push_back(slot);
		}
		catch(...)
		{
			m_bufferInUse[slot] = false;
			m_bufferAvailable.notify_one();
			return;
		}

		result = (*m_playerQueue)->Enqueue(
		    m_playerQueue,
		    m_buffers[slot].data(),
		    bufferSize);

		if(result != SL_RESULT_SUCCESS)
		{
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
	// Buffer ownership returns through QueueCallbackImpl().
	// Do not free slots here while OpenSL may still be playing them.
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
