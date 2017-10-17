#pragma once

#include "Transcode.hpp"

namespace av {

class GUIMediaFile : public AMTObject
{
	InputContext inputCtx;
	CodecContext codecCtx;
	AVStream *videoStream;
	SwsContext * swsctx;

	bool initialized;

	// OnFrameDecodedで直前にデコードされたフレーム
	// まだデコードしてない場合は-1
	int lastDecodeFrame;

	int64_t fileSize;
	int width, height;

	void MakeCodecContext() {
		AVCodecID vcodecId = videoStream->codecpar->codec_id;
		AVCodec *pCodec = avcodec_find_decoder(vcodecId);
		if (pCodec == NULL) {
			THROW(FormatException, "Could not find decoder ...");
		}
		codecCtx.Set(pCodec);
		if (avcodec_parameters_to_context(codecCtx(), videoStream->codecpar) != 0) {
			THROW(FormatException, "avcodec_parameters_to_context failed");
		}
		codecCtx()->thread_count = 4;
		if (avcodec_open2(codecCtx(), pCodec, NULL) != 0) {
			THROW(FormatException, "avcodec_open2 failed");
		}
	}

	void init(AVFrame* frame)
	{
		width = frame->width;
		height = frame->height;
		swsctx = sws_getCachedContext(NULL, width, height,
			AV_PIX_FMT_RGB24, width, height,
			(AVPixelFormat)frame->format, 0, 0, 0, 0);
	}

	void ConvertToRGB(uint8_t* rgb, int stride, AVFrame* frame)
	{
		uint8_t * inData[1] = { rgb };
		int inLinesize[1] = { stride };
		sws_scale(swsctx, inData, inLinesize, 0, height, frame->data, frame->linesize);
	}

	bool DecodeOneFrame(uint8_t* rgb, int stride) {
		Frame frame;
		AVPacket packet = AVPacket();
		bool ok = false;
		while (av_read_frame(inputCtx(), &packet) == 0) {
			if (packet.stream_index == videoStream->index) {
				if (avcodec_send_packet(codecCtx(), &packet) != 0) {
					THROW(FormatException, "avcodec_send_packet failed");
				}
				while (avcodec_receive_frame(codecCtx(), frame()) == 0) {
					// 最初はIフレームまでスキップ
					if (lastDecodeFrame != -1 || frame()->key_frame) {
						if (initialized == false) {
							init(frame());
							initialized = true;
						}
						if (rgb) {
							ConvertToRGB(rgb, stride, frame());
						}
						ok = true;
					}
				}
			}
			av_packet_unref(&packet);
			if (ok) {
				break;
			}
		}
		return ok;
	}

public:
	GUIMediaFile(AMTContext& ctx, const char* filepath)
		: AMTObject(ctx)
		, inputCtx(filepath)
		, initialized(false)
	{
		{
			File file(filepath, "rb");
			fileSize = file.size();
		}
		if (avformat_find_stream_info(inputCtx(), NULL) < 0) {
			THROW(FormatException, "avformat_find_stream_info failed");
		}
		videoStream = GetVideoStream(inputCtx());
		if (videoStream == NULL) {
			THROW(FormatException, "Could not find video stream ...");
		}
		lastDecodeFrame = -1;
		MakeCodecContext();
		DecodeOneFrame(nullptr, 0);
	}

	~GUIMediaFile() {
		sws_freeContext(swsctx);
		swsctx = nullptr;
	}

	int getWidth() const { return width; }
	int getHeight() const { return height; }

	bool getFrame(float pos, uint8_t* rgb, int stride) {
		ctx.setError(Exception());
		try {
			int64_t fileOffset = int64_t(fileSize * pos);
			if (av_seek_frame(inputCtx(), -1, fileOffset, AVSEEK_FLAG_BYTE) < 0) {
				THROW(FormatException, "av_seek_frame failed");
			}
			lastDecodeFrame = -1;
			MakeCodecContext();
			return DecodeOneFrame(rgb, stride);
		}
		catch (Exception& exception) {
			ctx.setError(exception);
		}
		return false;
	}
};

}

extern "C" __declspec(dllexport) void* MediaFile_Create(AMTContext* ctx, const char* filepath) {
	try {
		return new av::GUIMediaFile(*ctx, filepath);
	}
	catch (Exception& exception) {
		ctx->setError(exception);
	}
	return nullptr;
}
extern "C" __declspec(dllexport) void MediaFile_Delete(av::GUIMediaFile* ptr) { delete ptr; }
extern "C" __declspec(dllexport) int MediaFile_GetWidth(av::GUIMediaFile* ptr) { return ptr->getWidth(); }
extern "C" __declspec(dllexport) int MediaFile_GetHeight(av::GUIMediaFile* ptr) { return ptr->getHeight(); }
extern "C" __declspec(dllexport) bool MediaFile_GetFrame(av::GUIMediaFile* ptr, float pos, uint8_t* rgb, int stride) { return ptr->getFrame(pos, rgb, stride); }

class GUILogoFile
{
public:
};
