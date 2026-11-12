#pragma once

#include <chrono>
#include "Types.h"

class CFrameLimiter
{
public:
	CFrameLimiter() = default;
	~CFrameLimiter() = default;

	void BeginFrame();
	void EndFrame();

	void SetFrameRate(uint32 fps);

private:
	using Clock = std::chrono::steady_clock;
	using TimePoint = Clock::time_point;
	using Duration = Clock::duration;

	Duration m_minFrameDuration = Duration::zero();
	TimePoint m_frameStartTime{};
	bool m_frameStarted = false;
};
