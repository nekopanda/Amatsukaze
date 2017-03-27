/**
* Core transcoder
* Copyright (c) 2017 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include <string>
#include <vector>
#include <map>

#include "StreamUtils.hpp"
#include "ProcessThread.hpp"
#include "DCT.h"

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
	Frame()
		: frame_()
	{
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
	CodecContext(AVCodec* pCodec)
		: ctx_()
	{
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
	InputContext(const std::string& src)
		: ctx_()
	{
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
	WriteIOContext(int bufsize)
		: ctx_()
	{
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
	OutputContext(WriteIOContext& ioCtx, const char* format)
		: ctx_()
	{
		if (avformat_alloc_output_context2(&ctx_, NULL, format, "-") < 0) {
			THROW(FormatException, "avformat_alloc_output_context2 failed");
		}
		if (ctx_->pb != NULL) {
			THROW(FormatException, "pb already has ...");
		}
		ctx_->pb = ioCtx();
		// 10bit以上YUV4MPEG対応
		ctx_->strict_std_compliance = FF_COMPLIANCE_UNOFFICIAL;
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

class VideoReader : AMTObject
{
public:
	VideoReader(AMTContext& ctx)
		: AMTObject(ctx)
		, fmt_()
		, fieldMode_()
	{ }

	void readAll(const std::string& src)
	{
		InputContext inputCtx(src);
		if (avformat_find_stream_info(inputCtx(), NULL) < 0) {
			THROW(FormatException, "avformat_find_stream_info failed");
		}
		onFileOpen(inputCtx());
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

    bool first = true;
		Frame frame;
		AVPacket packet = AVPacket();
		while (av_read_frame(inputCtx(), &packet) == 0) {
			if (packet.stream_index == videoStream->index) {
				if (avcodec_send_packet(codecCtx(), &packet) != 0) {
					THROW(FormatException, "avcodec_send_packet failed");
				}
				while (avcodec_receive_frame(codecCtx(), frame()) == 0) {
          if (first) {
            onFirstFrame(videoStream, frame());
            first = false;
          }
					onFrame(frame);
				}
			}
      else {
        onAudioPacket(packet);
      }
			av_packet_unref(&packet);
		}

		// flush decoder
		if (avcodec_send_packet(codecCtx(), NULL) != 0) {
			THROW(FormatException, "avcodec_send_packet failed");
		}
		while (avcodec_receive_frame(codecCtx(), frame()) == 0) {
			onFrame(frame);
		}

	}

protected:
	virtual void onFileOpen(AVFormatContext *fmt) { };
	virtual void onVideoFormat(AVStream *stream, VideoFormat fmt) { };
  virtual void onFrameDecoded(Frame& frame) { };
  virtual void onAudioPacket(AVPacket& packet) { };

private:
  VideoFormat fmt_;
  bool fieldMode_;
  std::unique_ptr<av::Frame> prevFrame_;

  void onFrame(Frame& frame) {
    if (fieldMode_) {
			if (frame()->interlaced_frame == false) {
				// フレームがインタレースでなかったらそのまま出力
				prevFrame_ = nullptr;
				onFrameDecoded(frame);
			}
			else if (prevFrame_ == nullptr) {
				// トップフィールドでなかったら破棄
				// 仕様かどうかは不明だけどFFMPEG ver.3.2.2現在
				// top_field_first=1: top field
				// top_field_first=0: bottom field
				// となっているようである
				if (frame()->top_field_first) {
					prevFrame_ = std::unique_ptr<av::Frame>(new av::Frame(frame));
				}
				else {
					ctx.warn("トップフィールドを想定していたがそうではなかったのでフィールドを破棄");
				}
      }
      else {
        // 2枚のフィールドを合成
        auto merged = mergeFields(*prevFrame_, frame);
        onFrameDecoded(*merged);
        prevFrame_ = nullptr;
      }
    }
    else {
      onFrameDecoded(frame);
    }
  }

  void onFirstFrame(AVStream *stream, AVFrame *frame)
  {
    VIDEO_STREAM_FORMAT srcFormat = VS_UNKNOWN;
    switch (stream->codecpar->codec_id) {
    case AV_CODEC_ID_H264:
      srcFormat = VS_H264;
      break;
    case AV_CODEC_ID_HEVC:
      srcFormat = VS_H265;
      break;
    case AV_CODEC_ID_MPEG2VIDEO:
      srcFormat = VS_MPEG2;
      break;
    }
    
    fmt_.format = srcFormat;
    fmt_.progressive = !(frame->interlaced_frame);
    fmt_.width = frame->width;
    fmt_.height = frame->height;
    fmt_.sarWidth = frame->sample_aspect_ratio.num;
    fmt_.sarHeight = frame->sample_aspect_ratio.den;
    fmt_.colorPrimaries = frame->color_primaries;
    fmt_.transferCharacteristics = frame->color_trc;
    fmt_.colorSpace = frame->colorspace;
    // 今のところ固定フレームレートしか対応しない
    fmt_.fixedFrameRate = true;
    fmt_.frameRateNum = stream->avg_frame_rate.num;
    fmt_.frameRateDenom = stream->avg_frame_rate.den;

    // x265でインタレースの場合はfield mode
    fieldMode_ = (fmt_.format == VS_H265 && fmt_.progressive == false);

    if (fieldMode_) {
      fmt_.height *= 2;
      fmt_.frameRateNum /= 2;
    }

    onVideoFormat(stream, fmt_);
  }

  // 2つのフレームのトップフィールド、ボトムフィールドを合成
  static std::unique_ptr<av::Frame> mergeFields(av::Frame& topframe, av::Frame& bottomframe)
  {
    auto dstframe = std::unique_ptr<av::Frame>(new av::Frame());

    AVFrame* top = topframe();
    AVFrame* bottom = bottomframe();
    AVFrame* dst = (*dstframe)();

    // フレームのプロパティをコピー
    av_frame_copy_props(dst, top);

    // メモリサイズに関する情報をコピー
    dst->format = top->format;
    dst->width = top->width;
    dst->height = top->height * 2;

    // メモリ確保
    if (av_frame_get_buffer(dst, 64) != 0) {
      THROW(RuntimeException, "failed to allocate frame buffer");
    }

    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get((AVPixelFormat)(dst->format));
    int pixel_shift = (desc->comp[0].depth > 8) ? 1 : 0;

    for (int i = 0; i < 3; ++i) {
      int hshift = (i > 0) ? desc->log2_chroma_w : 0;
      int vshift = (i > 0) ? desc->log2_chroma_h : 0;
      int wbytes = (dst->width >> hshift) << pixel_shift;
      int height = dst->height >> vshift;

      for (int y = 0; y < height; y += 2) {
        uint8_t* dst0 = dst->data[i] + dst->linesize[i] * (y + 0);
        uint8_t* dst1 = dst->data[i] + dst->linesize[i] * (y + 1);
        uint8_t* src0 = top->data[i] + top->linesize[i] * (y >> 1);
        uint8_t* src1 = bottom->data[i] + bottom->linesize[i] * (y >> 1);
        memcpy(dst0, src0, wbytes);
        memcpy(dst1, src1, wbytes);
      }
    }

    return std::move(dstframe);
  }
};

class VideoWriter : NonCopyable
{
public:
	VideoWriter(VideoFormat fmt, int bufsize)
		: ioCtx_(this, bufsize)
		, outputCtx_(ioCtx_, "yuv4mpegpipe")
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

class AudioWriter : NonCopyable
{
public:
	AudioWriter(AVStream* src, int bufsize)
		: ioCtx_(this, bufsize)
		, outputCtx_(ioCtx_, "adts")
		, frameCount_(0)
	{
		AVStream* st = avformat_new_stream(outputCtx_(), NULL);
		if (st == NULL) {
			THROW(FormatException, "avformat_new_stream failed");
		}

		// コーデックパラメータをコピー
		avcodec_parameters_copy(st->codecpar, src->codecpar);

		// for debug
		av_dump_format(outputCtx_(), 0, "-", 1);

		if (avformat_write_header(outputCtx_(), NULL) < 0) {
			THROW(FormatException, "avformat_write_header failed");
		}
	}

	void inputFrame(AVPacket& frame) {
		// av_interleaved_write_frameにpacketのownershipを渡すので
		AVPacket outpacket = AVPacket();
		av_packet_ref(&outpacket, &frame);
		outpacket.stream_index = 0;
		outpacket.pos = -1;
		if (av_interleaved_write_frame(outputCtx_(), &outpacket) < 0) {
			THROW(FormatException, "av_interleaved_write_frame failed");
		}
	}

	void flush() {
		// flush muxer
		if(av_interleaved_write_frame(outputCtx_(), NULL) < 0) {
			THROW(FormatException, "av_interleaved_write_frame failed");
		}
	}

protected:
	virtual void onWrite(MemoryChunk mc) = 0;

private:
	class TransWriteContext : public WriteIOContext {
	public:
		TransWriteContext(AudioWriter* this_, int bufsize)
			: WriteIOContext(bufsize)
			, this_(this_)
		{ }
	protected:
		virtual void onWrite(MemoryChunk mc) {
			this_->onWrite(mc);
		}
	private:
		AudioWriter* this_;
	};

	TransWriteContext ioCtx_;
	OutputContext outputCtx_;

	int frameCount_;
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

  void start(const std::string& encoder_args, VideoFormat fmt, bool fieldMode, int bufsize) {
		if (videoWriter_ != NULL) {
			THROW(InvalidOperationException, "start method called multiple times");
		}
    fieldMode_ = fieldMode;
    if (fieldMode) {
      // フィールドモードのときは解像度は縦1/2でFPSは2倍
      fmt.height /= 2;
      fmt.frameRateNum *= 2;
    }
    videoWriter_ = new MyVideoWriter(this, fmt, bufsize);
		process_ = new MySubProcess(this, encoder_args);
	}

	void inputFrame(Frame& frame) {
		if (videoWriter_ == NULL) {
			THROW(InvalidOperationException, "you need to call start method before input frame");
    }
    if (fieldMode_) {
      // フィールドモードのときはtop,bottomの2つに分けて出力
      av::Frame top = av::Frame();
      av::Frame bottom = av::Frame();
      splitFrameToFields(frame, top, bottom);
      videoWriter_->inputFrame(top);
      videoWriter_->inputFrame(bottom);
    }
    else {
      videoWriter_->inputFrame(frame);
    }
	}

	void finish() {
		if (videoWriter_ != NULL) {
			videoWriter_->flush();
			process_->finishWrite();
			int ret = process_->join();
			if (ret != 0) {
				THROWF(RuntimeException, "encode failed (encoder exit code: %d)", ret);
			}
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
  bool fieldMode_;

	void onProcessOut(bool isErr, MemoryChunk mc) {
		// これはマルチスレッドで呼ばれるの注意
		fwrite(mc.data, mc.length, 1, isErr ? stderr : stdout);
		fflush(isErr ? stderr : stdout);
	}
	void onVideoWrite(MemoryChunk mc) {
		process_->write(mc);
  }

  VideoFormat getEncoderInputVideoFormat(VideoFormat format) {
    if (fieldMode_) {
      // フィールドモードのときは解像度は縦1/2でFPSは2倍
      format.height /= 2;
      format.frameRateNum *= 2;
    }
    return format;
  }

  // 1つのフレームをトップフィールド、ボトムフィールドの2つのフレームに分解
  static void splitFrameToFields(av::Frame& frame, av::Frame& topfield, av::Frame& bottomfield)
  {
    AVFrame* src = frame();
    AVFrame* top = topfield();
    AVFrame* bottom = bottomfield();

    // フレームのプロパティをコピー
    av_frame_copy_props(top, src);
    av_frame_copy_props(bottom, src);

    // メモリサイズに関する情報をコピー
    top->format = bottom->format = src->format;
    top->width = bottom->width = src->width;
    top->height = bottom->height = src->height / 2;

    // メモリ確保
    if (av_frame_get_buffer(top, 64) != 0) {
      THROW(RuntimeException, "failed to allocate frame buffer");
    }
    if (av_frame_get_buffer(bottom, 64) != 0) {
      THROW(RuntimeException, "failed to allocate frame buffer");
    }

    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get((AVPixelFormat)(src->format));
    int pixel_shift = (desc->comp[0].depth > 8) ? 1 : 0;

    for (int i = 0; i < 3; ++i) {
      int hshift = (i > 0) ? desc->log2_chroma_w : 0;
      int vshift = (i > 0) ? desc->log2_chroma_h : 0;
      int wbytes = (src->width >> hshift) << pixel_shift;
      int height = src->height >> vshift;

      for (int y = 0; y < height; y += 2) {
        uint8_t* src0 = src->data[i] + src->linesize[i] * (y + 0);
        uint8_t* src1 = src->data[i] + src->linesize[i] * (y + 1);
        uint8_t* dst0 = top->data[i] + top->linesize[i] * (y >> 1);
        uint8_t* dst1 = bottom->data[i] + bottom->linesize[i] * (y >> 1);
        memcpy(dst0, src0, wbytes);
        memcpy(dst1, src1, wbytes);
      }
    }
  }
};

// 映像内容解析
class VideoAnalyzer : public VideoReader
{
public:
  VideoAnalyzer(AMTContext& ctx)
    : VideoReader(ctx)
  { }

  void dump() {
    FILE* fp = fopen("dct.txt", "w");
    for (const auto& s : frames_) {
      s.print(fp);
    }
    fclose(fp);
  }

protected:
  VideoFormat fmt_;
  std::unique_ptr<av::Frame> prevFrame_;

  virtual void onFileOpen(AVFormatContext *fmt) { };
  
  virtual void onVideoFormat(AVStream *stream, VideoFormat fmt) {
    fmt_ = fmt;
  };

  virtual void onFrameDecoded(Frame& frame) {
    if (prevFrame_ != nullptr) {
      analyzeFrame(frame(), (*prevFrame_)());
      printf("frame=%d\n", frames_.size());
    }
    prevFrame_ = std::unique_ptr<av::Frame>(new av::Frame(frame));
  };

  virtual void onAudioPacket(AVPacket& packet) { };

private:
  enum { DCT_N = 8 };
  Dct2d<DCT_N> dct_;

  struct DctSummary {
    enum { LEN = DCT_N * 2 - 1 };
    float summary[LEN];

    void print(FILE* fp) const {
      for (int i = 0; i < LEN; ++i) {
        fprintf(fp, (i == LEN - 1) ? "%f\n" : "%f,", summary[i]);
      }
    }
  };

  std::vector<DctSummary> frames_;

  template <typename T>
  void fill_block(float block[][DCT_N], uint8_t* cptr, int cline, uint8_t* pptr, int pline)
  {
    for (int sy = 0, dy = 0; dy < DCT_N; sy += 2, dy += 1) {
      T* cptr_line = (T*)(cptr + sy * cline);
      T* pptr_line = (T*)(pptr + sy * pline);

      for (int sx = 0, dx = 0; dx < DCT_N; sx += 2, dx += 1) {
        block[dy][dx] = std::abs((float)cptr_line[sx] - (float)pptr_line[sx]);
      }
    }
  }

  void addToSummary(float block[][DCT_N], DctSummary& s)
  {
    for (int y = 0; y < DCT_N; ++y) {
      for (int x = 0; x < DCT_N; ++x) {
        s.summary[y + x] += std::abs(block[y][x]);
      }
    }
  }

  void analyzeFrame(AVFrame* cur, AVFrame* prev)
  {
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get((AVPixelFormat)(cur->format));
    int pixel_shift = (desc->comp[0].depth > 8) ? 1 : 0;

    DctSummary summary = DctSummary();

    for (int i = 0; i < 3; ++i) {
      int hshift = (i > 0) ? desc->log2_chroma_w : 0;
      int vshift = (i > 0) ? desc->log2_chroma_h : 0;
      int width = cur->width >> hshift;
      int height = cur->height >> vshift;

      for (int y = 0; y <= (height - DCT_N * 2); y += DCT_N * 2) {
        int cline = cur->linesize[i];
        int pline = prev->linesize[i];
        uint8_t* cptr = cur->data[i] + cline * y;
        uint8_t* pptr = prev->data[i] + pline * y;

        for (int x = 0; x <= (width - DCT_N * 2); x += DCT_N * 2) {
          float src[DCT_N][DCT_N] = { 0 };
          float dct[DCT_N][DCT_N];

          if (pixel_shift == 0) {
            fill_block<uint8_t>(src, cptr, cline, pptr, pline);
          }
          else {
            fill_block<uint16_t>(src, cptr, cline, pptr, pline);
          }

          dct_.transform_AVX(src, dct);

          addToSummary(dct, summary);
        }
      }
    }

    frames_.push_back(summary);
  }
};

// エンコーダテスト用クラス
class MultiOutTranscoder : AMTObject
{
public:
	MultiOutTranscoder(AMTContext& ctx)
		: AMTObject(ctx)
	{ }

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
			encoders_[i].start(encoder_args[i], formats[i], false, bufsize);
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
			: VideoReader(this_->ctx)
			, this_(this_)
		{ }
	protected:
    virtual void onVideoFormat(AVStream *stream, VideoFormat fmt) { }
    virtual void onFrameDecoded(av::Frame& frame) {
      this_->onFrameDecoded(frame);
    }
    virtual void onAudioPacket(AVPacket& packet) { }
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

		// copy reference
		Frame frame = frame__;

		int index = getEncoderIndex(frame);
		encoders_[index].inputFrame(frame);
	}
};

} // namespace av

