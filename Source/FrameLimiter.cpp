#include "FrameLimiter.h"

#include <thread>

void CFrameLimiter::BeginFrame()
{
	m_frameStartTime = Clock::now();
	m_frameStarted = true;
}

void CFrameLimiter::EndFrame()
{
	if(!m_frameStarted)
	{
		return;
	}

	m_frameStarted = false;

	if(m_minFrameDuration <= Duration::zero())
	{
		return;
	}

	const auto targetFrameEnd = m_frameStartTime + m_minFrameDuration;
	const auto now = Clock::now();

	// Do not sleep when emulation already exceeded the frame budget.
	if(now >= targetFrameEnd)
	{
		return;
	}

	// Use an absolute deadline to avoid accumulating sleep drift.
	std::this_thread::sleep_until(targetFrameEnd);
}

void CFrameLimiter::SetFrameRate(uint32 fps)
{
	if(fps == 0)
	{
		m_minFrameDuration = Duration::zero();
		return;
	}

	// Keep fractional clock ticks to avoid integer-microsecond truncation.
	const auto frameDuration =
	    std::chrono::duration_cast<Duration>(
	        std::chrono::duration<double>(1.0 / static_cast<double>(fps)));

	m_minFrameDuration = frameDuration;
}
