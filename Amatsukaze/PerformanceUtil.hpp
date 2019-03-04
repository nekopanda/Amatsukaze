/**
* Amtasukaze Performance Utility
* Copyright (c) 2017-2019 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include "StreamUtils.hpp"

class Stopwatch
{
	int64_t sum;
	int64_t prev;
	int64_t freq;
public:
	Stopwatch()
		: sum(0)
	{
		QueryPerformanceFrequency((LARGE_INTEGER*)&freq);
	}

	void reset() {
		sum = 0;
	}

	void start() {
		QueryPerformanceCounter((LARGE_INTEGER*)&prev);
	}

	double current() {
		int64_t cur;
		QueryPerformanceCounter((LARGE_INTEGER*)&cur);
		return (double)(cur - prev) / freq;
	}

	void stop() {
		int64_t cur;
		QueryPerformanceCounter((LARGE_INTEGER*)&cur);
		sum += cur - prev;
		prev = cur;
	}

	double getTotal() const {
		return (double)sum / freq;
	}

	double getAndReset() {
		stop();
		double ret = getTotal();
		sum = 0;
		return ret;
	}
};

class FpsPrinter : AMTObject
{
	struct TimeCount {
		float span;
		int count;
	};

	Stopwatch sw;
	std::deque<TimeCount> times;
	int navg;
	int total;

	TimeCount sum;
	TimeCount current;

	void updateProgress(bool last) {
		sum.span += current.span;
		sum.count += current.count;
		times.push_back(current);
		current = TimeCount();

		if (last) {
			ctx.infoF("complete. %.2ffps (%dÉtÉåÅ[ÉÄ)", sum.count / sum.span, total);
		}
		else {
			float sumtime = 0;
			int sumcount = 0;
			for (int i = 0; i < (int)times.size(); ++i) {
				sumtime += times[i].span;
				sumcount += times[i].count;
			}
			ctx.progressF("%d/%d %.2ffps", sum.count, total, sumcount / sumtime);

			if ((int)times.size() > navg) {
				times.pop_front();
			}
		}
	}
public:
	FpsPrinter(AMTContext& ctx, int navg)
		: AMTObject(ctx)
		, navg(navg)
	{ }

	void start(int total_) {
		total = total_;
		sum = TimeCount();
		current = TimeCount();
		times.clear();

		sw.start();
	}

	void update(int count) {
		current.count += count;
		current.span += (float)sw.getAndReset();
		if (current.span >= 0.5f) {
			updateProgress(false);
		}
	}

	void stop() {
		current.span += (float)sw.getAndReset();
		updateProgress(true);
		sw.stop();
	}
};

