#pragma once

#include <memory>
#include <deque>

#include "Transcode.hpp"
#include "CudaFilter.h"

class VideoFilter : NonCopyable
{
public:
	VideoFilter() : nextFilter(NULL) { }

	VideoFilter* nextFilter;

	virtual void start() = 0;
	virtual void onFrame(std::unique_ptr<av::Frame>&& frame) = 0;
	virtual void finish() = 0;

	void sendFrame(std::unique_ptr<av::Frame>&& frame) {
		if (nextFilter != NULL) {
			nextFilter->onFrame(std::move(frame));
		}
	}
};

class TemporalNRFilter : public VideoFilter
{
public:
	TemporalNRFilter()
	{ }
	
	void init(int temporalDistance, int threshold, bool interlaced) {
		NFRAMES_ = temporalDistance * 2 + 1;
		DIFFMAX_ = threshold;
		interlaced_ = interlaced;

		if (NFRAMES_ > MAX_NFRAMES) {
			THROW(InvalidOperationException, "TemporalNRFilter最大枚数を超えています");
		}
	}
	virtual void start() {
		//
	}
	virtual void onFrame(std::unique_ptr<av::Frame>&& frame)
	{
		int format = (*frame)()->format;
		switch (format) {
		case AV_PIX_FMT_YUV420P:
		case AV_PIX_FMT_YUV420P10LE:
		case AV_PIX_FMT_YUV420P12LE:
		case AV_PIX_FMT_YUV420P14LE:
		case AV_PIX_FMT_YUV420P16LE:
			break;
		default:
			THROW(FormatException, "未対応フォーマットです");
		}

		frames_.emplace_back(std::move(frame));

		int half = (NFRAMES_ + 1) / 2;
		if (frames_.size() < half) {
			return;
		}

		AVFrame* frames[MAX_NFRAMES];
		for (int i = 0, f = (int)frames_.size() - NFRAMES_; i < NFRAMES_; ++i, ++f) {
			frames[i] = (*frames_[std::max(f, 0)])();
		}
		sendFrame(TNRFilter(frames, frames_[frames_.size() - half]->frameIndex_));

		if (frames_.size() >= NFRAMES_) {
			frames_.pop_front();
		}
	}
	virtual void finish() {
		int half = NFRAMES_ / 2;
		AVFrame* frames[MAX_NFRAMES];

		while (frames_.size() > half) {
			for (int i = 0; i < NFRAMES_; ++i) {
				frames[i] = (*frames_[std::min(i, (int)frames_.size() - 1)])();
			}
			sendFrame(TNRFilter(frames, frames_[half]->frameIndex_));

			frames_.pop_front();
		}
	}

private:
	enum { MAX_NFRAMES = 128 };

	std::deque<std::unique_ptr<av::Frame>> frames_;

	bool interlaced_;
	int NFRAMES_;
	int DIFFMAX_;

	std::unique_ptr<av::Frame> TNRFilter(AVFrame** frames, int frameIndex)
	{
		auto dstframe = std::unique_ptr<av::Frame>(new av::Frame(frameIndex));

		AVFrame* top = frames[0];
		AVFrame* dst = (*dstframe)();

		// フレームのプロパティをコピー
		av_frame_copy_props(dst, top);

		// メモリサイズに関する情報をコピー
		dst->format = top->format;
		dst->width = top->width;
		dst->height = top->height;

		// メモリ確保
		if (av_frame_get_buffer(dst, 64) != 0) {
			THROW(RuntimeException, "failed to allocate frame buffer");
		}

		const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get((AVPixelFormat)(top->format));
		int thresh = DIFFMAX_ << (desc->comp[0].depth - 8);

		float kernel[MAX_NFRAMES];
		for (int i = 0; i < NFRAMES_; ++i) {
			kernel[i] = 1;
		}

		if (desc->comp[0].depth > 8) {
			filterKernel<uint16_t>(frames, dst, interlaced_, thresh, kernel);
		}
		else {
			filterKernel<uint8_t>(frames, dst, interlaced_, thresh, kernel);
		}

		return std::move(dstframe);
	}

	template <typename T>
	T getPixel(AVFrame* frame, int idx, int x, int y) {
		return *((T*)(frame->data[idx] + frame->linesize[idx] * y) + x);
	}

	template <typename T>
	void setPixel(AVFrame* frame, int idx, int x, int y, T v) {
		*((T*)(frame->data[idx] + frame->linesize[idx] * y) + x) = v;
	}

	template <typename T>
	int calcDiff(T Y, T U, T V, T rY, T rU, T rV) {
		return
			std::abs((int)Y - (int)rY) +
			std::abs((int)U - (int)rU) +
			std::abs((int)V - (int)rV);
	}

	template <typename T>
	void filterKernel(AVFrame** frames, AVFrame* dst, bool interlaced, int thresh, float* kernel)
	{
		int mid = NFRAMES_ / 2;
		int width = frames[0]->width;
		int height = frames[0]->height;

		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				int cy = interlaced ? (((y >> 1) & ~1) | (y & 1)) : (y >> 1);
				int cx = x >> 1;

				T Y = getPixel<T>(frames[mid], 0, x, y);
				T U = getPixel<T>(frames[mid], 1, cx, cy);
				T V = getPixel<T>(frames[mid], 2, cx, cy);

				float sumKernel = 0.0f;
				for (int i = 0; i < NFRAMES_; ++i) {
					T rY = getPixel<T>(frames[i], 0, x, y);
					T rU = getPixel<T>(frames[i], 1, cx, cy);
					T rV = getPixel<T>(frames[i], 2, cx, cy);

					int diff = calcDiff(Y, U, V, rY, rU, rV);
					if (diff <= thresh) {
						sumKernel += kernel[i];
					}
				}

				float factor = 1.f / sumKernel;

				float dY = 0.5f;
				float dU = 0.5f;
				float dV = 0.5f;
				for (int i = 0; i < NFRAMES_; ++i) {
					T rY = getPixel<T>(frames[i], 0, x, y);
					T rU = getPixel<T>(frames[i], 1, cx, cy);
					T rV = getPixel<T>(frames[i], 2, cx, cy);

					int diff = calcDiff(Y, U, V, rY, rU, rV);
					if (diff <= thresh) {
						float coef = kernel[i] * factor;
						dY += coef * rY;
						dU += coef * rU;
						dV += coef * rV;
					}
				}

				setPixel(dst, 0, x, y, (T)dY);

				bool cout = (((x & 1) == 0) && (((interlaced ? (y >> 1) : y) & 1) == 0));
				if (cout) {
					setPixel(dst, 1, cx, cy, (T)dU);
					setPixel(dst, 2, cx, cy, (T)dV);
				}
			}
		}
	}
};

class CudaTemporalNRFilter : public VideoFilter
{
public:
	CudaTemporalNRFilter() : filter_(NULL), frame_(-1)
	{ }

	void init(int temporalDistance, int threshold, int batchSize, int interlaced) {
		filter_ = cudaTNRCreate(temporalDistance, threshold, batchSize, interlaced);
		if (filter_ == NULL) {
			THROW(RuntimeException, "cudaTNRCreateに失敗");
		}
	}
	virtual void start() { }
	virtual void onFrame(std::unique_ptr<av::Frame>&& frame)
	{
		if (filter_ == NULL) {
			THROW(InvalidOperationException, "initを呼んでください");
		}
		if (cudaTNRSendFrame(filter_, (*frame)()) != 0) {
			THROW(RuntimeException, "cudaTNRSendFrameに失敗");
		}
		frameQueue_.emplace_back(std::move(frame));
		while(cudaTNRRecvFrame(filter_, frame_()) == 0) {
			av_frame_copy_props(frame_(), (*frameQueue_.front())());
			frame_.frameIndex_ = frameQueue_.front()->frameIndex_;
			frameQueue_.pop_front();
			sendFrame(std::unique_ptr<av::Frame>(new av::Frame(frame_)));
		}
	}
	virtual void finish() {
		if (filter_ == NULL) {
			THROW(InvalidOperationException, "initを呼んでください");
		}
		if (cudaTNRFinish(filter_) != 0) {
			THROW(RuntimeException, "cudaTNRFinishに失敗");
		}
		while (cudaTNRRecvFrame(filter_, frame_()) == 0) {
			if (frameQueue_.size() == 0) {
				THROW(RuntimeException, "フィルタでフレームが増加しました");
			}
			av_frame_copy_props(frame_(), (*frameQueue_.front())());
			frame_.frameIndex_ = frameQueue_.front()->frameIndex_;
			frameQueue_.pop_front();
			sendFrame(std::unique_ptr<av::Frame>(new av::Frame(frame_)));
		}
		if (frameQueue_.size() != 0) {
			THROW(RuntimeException, "フィルタでフレームが減少しました");
		}
	}
private:
	CudaTNRFilter filter_;
	av::Frame frame_;
	std::deque<std::unique_ptr<av::Frame>> frameQueue_;
};
