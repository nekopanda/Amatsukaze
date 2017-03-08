#pragma once

#include <string>
#include <vector>
#include <map>

#include "StreamUtils.hpp"
#include "ProcessThread.hpp"

// libffmpeg
extern "C" {
#include <libavutil/imgutils.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avformat.lib")

namespace av {

AVStream* GetVideoStream(AVFormatContext* pCtx)
{
	for (int i = 0; i < (int)pCtx->nb_streams; ++i) {
		if (pCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			return pCtx->streams[i];
		}
	}
	return NULL;
}

AVFrame* AllocPicture(AVPixelFormat fmt, int width, int height)
{
	// AVFrame確保
	AVFrame *pPicture = av_frame_alloc();
	// バッファ確保
	ASSERT(av_image_alloc(pPicture->data, pPicture->linesize, width, height, fmt, 32) >= 0);

	return pPicture;
}

void FreePicture(AVFrame*& pPicture)
{
	av_freep(&pPicture->data[0]);
	av_frame_free(&pPicture);
	pPicture = NULL;
}

class Frame {
public:
	Frame() {
		frame_ = av_frame_alloc();
	}
	Frame(const Frame& src) {
		frame_ = av_frame_alloc();
		av_frame_ref(frame_, src());
	}
	~Frame() {
		av_frame_free(&frame_);
	}
	AVFrame* operator()() {
		return frame_;
	}
	const AVFrame* operator()() const {
		return frame_;
	}
	Frame& operator=(const Frame& src) {
		av_frame_unref(frame_);
		av_frame_ref(frame_, src());
	}
private:
	AVFrame* frame_;
};

class CodecContext : NonCopyable {
public:
	CodecContext(AVCodec* pCodec) {
		if (pCodec == NULL) {
			THROW(RuntimeException, "pCodec is NULL");
		}
		ctx_ = avcodec_alloc_context3(pCodec);
		if (ctx_ == NULL) {
			THROW(IOException, "failed avcodec_alloc_context3");
		}
	}
	~CodecContext() {
		avcodec_free_context(&ctx_);
	}
	AVCodecContext* operator()() {
		return ctx_;
	}
private:
	AVCodecContext *ctx_;
};

class InputContext : NonCopyable {
public:
	InputContext(const std::string& src) {
		if (avformat_open_input(&ctx_, src.c_str(), NULL, NULL) != 0) {
			THROW(IOException, "failed avformat_open_input");
		}
	}
	~InputContext() {
		avformat_close_input(&ctx_);
	}
	AVFormatContext* operator()() {
		return ctx_;
	}
private:
	AVFormatContext* ctx_;
};

class WriteIOContext : NonCopyable {
public:
	WriteIOContext(int bufsize) {
		unsigned char* buffer = (unsigned char*)av_malloc(bufsize);
		ctx_ = avio_alloc_context(buffer, bufsize, 1, this, NULL, write_packet_, NULL);
	}
	~WriteIOContext() {
		av_free(ctx_->buffer);
		av_free(ctx_);
	}
	AVIOContext* operator()() {
		return ctx_;
	}
protected:
	virtual void onWrite(MemoryChunk mc) = 0;
private:
	AVIOContext* ctx_;
	static int write_packet_(void *opaque, uint8_t *buf, int buf_size) {
		((WriteIOContext*)opaque)->onWrite(MemoryChunk(buf, buf_size));
		return 0;
	}
};

class OutputContext : NonCopyable {
public:
	OutputContext(WriteIOContext& ioCtx) {
		if (avformat_alloc_output_context2(&ctx_, NULL, "yuv4mpegpipe", "-") < 0) {
			THROW(FormatException, "avformat_alloc_output_context2 failed");
		}
		if (ctx_->pb != NULL) {
			THROW(FormatException, "pb already has ...");
		}
		ctx_->pb = ioCtx();
	}
	~OutputContext() {
		avformat_free_context(ctx_);
	}
	AVFormatContext* operator()() {
		return ctx_;
	}
private:
	AVFormatContext* ctx_;
};

class VideoReader : NonCopyable
{
public:
	void readAll(const std::string& src)
	{
		InputContext inputCtx(src);
		if (avformat_find_stream_info(inputCtx(), NULL) < 0) {
			THROW(FormatException, "avformat_find_stream_info failed");
		}
		AVStream *videoStream = GetVideoStream(inputCtx());
		if (videoStream == NULL) {
			THROW(FormatException, "Could not find video stream ...");
		}
		AVCodec *pCodec = avcodec_find_decoder(videoStream->codecpar->codec_id);
		if (pCodec == NULL) {
			THROW(FormatException, "Could not find decoder ...");
		}
		CodecContext codecCtx(pCodec);
		if (avcodec_parameters_to_context(codecCtx(), videoStream->codecpar) != 0) {
			THROW(FormatException, "avcodec_parameters_to_context failed");
		}
		if (avcodec_open2(codecCtx(), pCodec, NULL) != 0) {
			THROW(FormatException, "avcodec_open2 failed");
		}

		Frame frame;
		AVPacket packet = AVPacket();
		while (av_read_frame(inputCtx(), &packet) == 0) {
			if (packet.stream_index == videoStream->index) {
				if (avcodec_send_packet(codecCtx(), &packet) != 0) {
					THROW(FormatException, "avcodec_send_packet failed");
				}
				while (avcodec_receive_frame(codecCtx(), frame()) == 0) {
					onFrameDecoded(frame);
				}
			}
			av_packet_unref(&packet);
		}

		// flush decoder
		if (avcodec_send_packet(codecCtx(), NULL) != 0) {
			THROW(FormatException, "avcodec_send_packet failed");
		}
		while (avcodec_receive_frame(codecCtx(), frame()) == 0) {
			onFrameDecoded(frame);
		}
	}

protected:
	virtual void onFrameDecoded(Frame& frame) = 0;

};

class VideoWriter : NonCopyable
{
public:
	VideoWriter(VideoFormat fmt, int bufsize)
		: ioCtx_(this, bufsize)
		, outputCtx_(ioCtx_)
		, codecCtx_(avcodec_find_encoder_by_name("wrapped_avframe"))
		, fmt_(fmt)
		, initialized_(false)
		, frameCount_(0)
	{ }

	void inputFrame(Frame& frame) {

		// フォーマットチェック
		ASSERT(fmt_.width == frame()->width);
		ASSERT(fmt_.height == frame()->height);
		ASSERT(fmt_.sarWidth == frame()->sample_aspect_ratio.num);
		ASSERT(fmt_.sarHeight == frame()->sample_aspect_ratio.den);
		ASSERT(fmt_.colorPrimaries == frame()->color_primaries);
		ASSERT(fmt_.transferCharacteristics == frame()->color_trc);
		ASSERT(fmt_.colorSpace == frame()->colorspace);

		// PTS再定義
		frame()->pts = frameCount_++;

		init(frame);

		if (avcodec_send_frame(codecCtx_(), frame()) != 0) {
			THROW(FormatException, "avcodec_send_frame failed");
		}
		AVPacket packet = AVPacket();
		while (avcodec_receive_packet(codecCtx_(), &packet) == 0) {
			packet.stream_index = 0;
			av_interleaved_write_frame(outputCtx_(), &packet);
			av_packet_unref(&packet);
		}
	}

	void flush() {
		if (initialized_) {
			// flush encoder
			if (avcodec_send_frame(codecCtx_(), NULL) != 0) {
				THROW(FormatException, "avcodec_send_frame failed");
			}
			AVPacket packet = AVPacket();
			while (avcodec_receive_packet(codecCtx_(), &packet) == 0) {
				packet.stream_index = 0;
				av_interleaved_write_frame(outputCtx_(), &packet);
				av_packet_unref(&packet);
			}
			// flush muxer
			av_interleaved_write_frame(outputCtx_(), NULL);
		}
	}

protected:
	virtual void onWrite(MemoryChunk mc) = 0;

private:
	class TransWriteContext : public WriteIOContext {
	public:
		TransWriteContext(VideoWriter* this_, int bufsize)
			: WriteIOContext(bufsize)
			, this_(this_)
		{ }
	protected:
		virtual void onWrite(MemoryChunk mc) {
			this_->onWrite(mc);
		}
	private:
		VideoWriter* this_;
	};

	TransWriteContext ioCtx_;
	OutputContext outputCtx_;
	CodecContext codecCtx_;
	VideoFormat fmt_;

	bool initialized_;
	int frameCount_;

	void init(Frame& frame)
	{
		if (initialized_ == false) {
			AVStream* st = avformat_new_stream(outputCtx_(), NULL);
			if (st == NULL) {
				THROW(FormatException, "avformat_new_stream failed");
			}
			// time_base
			AVCodecContext* enc = codecCtx_();

			enc->pix_fmt = (AVPixelFormat)frame()->format;
			enc->width = frame()->width;
			enc->height = frame()->height;
			enc->field_order = fmt_.progressive ? AV_FIELD_PROGRESSIVE : AV_FIELD_TT;
			enc->color_range = frame()->color_range;
			enc->color_primaries = frame()->color_primaries;
			enc->color_trc = frame()->color_trc;
			enc->colorspace = frame()->colorspace;
			enc->chroma_sample_location = frame()->chroma_location;
			st->sample_aspect_ratio = enc->sample_aspect_ratio = frame()->sample_aspect_ratio;

			st->time_base = enc->time_base = av_make_q(fmt_.frameRateDenom, fmt_.frameRateNum);
			st->avg_frame_rate = av_make_q(fmt_.frameRateNum, fmt_.frameRateDenom);
			
			if (avcodec_open2(codecCtx_(), codecCtx_()->codec, NULL) != 0) {
				THROW(FormatException, "avcodec_open2 failed");
			}

			// muxerにエンコーダパラメータを渡す
			avcodec_parameters_from_context(st->codecpar, enc);
			
			// for debug
			av_dump_format(outputCtx_(), 0, "-", 1);

			if (avformat_write_header(outputCtx_(), NULL) < 0) {
				THROW(FormatException, "avformat_write_header failed");
			}
			initialized_ = true;
		}
	}
};

class EncodeWriter : NonCopyable
{
public:
	EncodeWriter()
		: videoWriter_(NULL)
		, process_(NULL)
	{ }
	~EncodeWriter()
	{
		if (process_ != NULL && process_->isRunning()) {
			THROW(InvalidOperationException, "call finish before destroy object ...");
		}

		delete videoWriter_;
		delete process_;
	}

	void start(const std::string& encoder_args, VideoFormat fmt, int bufsize) {
		if (videoWriter_ != NULL) {
			THROW(InvalidOperationException, "start method called multiple times");
		}
		videoWriter_ = new MyVideoWriter(this, fmt, bufsize);
		process_ = new MySubProcess(this, encoder_args);
	}

	void inputFrame(Frame& frame) {
		if (videoWriter_ == NULL) {
			THROW(InvalidOperationException, "you need to call start method before input frame");
		}
		videoWriter_->inputFrame(frame);
	}

	void finish() {
		if (videoWriter_ != NULL) {
			videoWriter_->flush();
			process_->finishWrite();
			process_->join();
		}
	}

private:
	class MyVideoWriter : public VideoWriter {
	public:
		MyVideoWriter(EncodeWriter* this_, VideoFormat fmt, int bufsize)
			: VideoWriter(fmt, bufsize)
			, this_(this_) 
		{ }
	protected:
		virtual void onWrite(MemoryChunk mc) {
			this_->onVideoWrite(mc);
		}
	private:
		EncodeWriter* this_;
	};

	class MySubProcess : public EventBaseSubProcess {
	public:
		MySubProcess(EncodeWriter* this_, const std::string& args)
			: EventBaseSubProcess(args)
			, this_(this_)
		{ }
	protected:
		virtual void onOut(bool isErr, MemoryChunk mc) {
			this_->onProcessOut(isErr, mc);
		}
	private:
		EncodeWriter* this_;
	};

	MyVideoWriter* videoWriter_;
	MySubProcess* process_;

	void onProcessOut(bool isErr, MemoryChunk mc) {
		// これはマルチスレッドで呼ばれるの注意
		fwrite(mc.data, mc.length, 1, isErr ? stderr : stdout);
	}
	void onVideoWrite(MemoryChunk mc) {
		process_->write(mc);
	}
};

class MultiOutTranscoder
{
public:

	void encode(
		const std::string& srcpath,
		const std::vector<VideoFormat>& formats,
		const std::vector<std::string>& encoder_args,
		const std::map<int64_t, int>& frameMap, 
		int bufsize)
	{
		numEncoders_ = (int)encoder_args.size();
		encoders_ = new EncodeWriter[numEncoders_];
		frameMap_ = &frameMap;

		MyVideoReader reader(this);

		for (int i = 0; i < numEncoders_; ++i) {
			encoders_[i].start(encoder_args[i], formats[i], bufsize);
		}

		reader.readAll(srcpath);

		for (int i = 0; i < numEncoders_; ++i) {
			encoders_[i].finish();
		}

		delete[] encoders_;

		numEncoders_ = 0;
		encoders_ = NULL;
		frameMap_ = NULL;
	}

private:
	class MyVideoReader : public VideoReader {
	public:
		MyVideoReader(MultiOutTranscoder* this_)
			: VideoReader()
			, this_(this_)
		{ }
	protected:
		virtual void onFrameDecoded(Frame& frame) {
			this_->onFrameDecoded(frame);
		}
	private:
		MultiOutTranscoder* this_;
	};

	bool test_enabled = true;

	int numEncoders_;
	EncodeWriter* encoders_;
	const std::map<int64_t, int>* frameMap_;

	int getEncoderIndex(Frame& frame) {
		int64_t pts = frame()->pts;
		auto it = frameMap_->find(pts);
		if (it == frameMap_->end()) {
			if (test_enabled) {
				return 0;
			}
			THROWF(RuntimeException, "Unknown PTS frame %lld", pts);
		}
		if (it->second >= numEncoders_) {
			THROWF(RuntimeException,
				"Encoder number(%d) exceed numEncoders(%d) at %lld",
				it->second, numEncoders_, pts);
		}
		return it->second;
	}

	void onFrameDecoded(Frame& frame__) {

		// TODO: thread
		// copy reference
		Frame frame = frame__;

		int index = getEncoderIndex(frame);
		encoders_[index].inputFrame(frame);
	}
};

} // namespace av

