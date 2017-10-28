#pragma once

#include "StreamUtils.hpp"

class Stopwatch
{
	int64_t sum;
	int64_t prev;
public:
	Stopwatch()
		: sum(0)
	{
		//
	}

	void reset() {
		sum = 0;
	}

	void start() {
		QueryPerformanceCounter((LARGE_INTEGER*)&prev);
	}

	void stop() {
		int64_t cur;
		QueryPerformanceCounter((LARGE_INTEGER*)&cur);
		sum += cur - prev;
		prev = cur;
	}

	double getTotal() const {
		int64_t freq;
		QueryPerformanceFrequency((LARGE_INTEGER*)&freq);
		return (double)sum / freq;
	}

	double getAndReset() {
		stop();
		double ret = getTotal();
		sum = 0;
		return ret;
	}
};
