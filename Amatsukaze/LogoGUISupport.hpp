#pragma once

#include "Transcode.hpp"
#include "LogoScan.hpp"

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
			(AVPixelFormat)frame->format, width, height,
			AV_PIX_FMT_BGR24, 0, 0, 0, 0);
	}

	void ConvertToRGB(uint8_t* rgb, int stride, AVFrame* frame)
	{
		uint8_t * outData[1] = { rgb };
		int outLinesize[1] = { stride };
		sws_scale(swsctx, frame->data, frame->linesize, 0, height, outData, outLinesize);
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

} // namespace av

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

namespace logo {

class GUILogoFile : public AMTObject
{
	LogoHeader header;
	LogoDataParam logo;
public:
	GUILogoFile(AMTContext& ctx, const char* filename)
		: AMTObject(ctx)
		, logo(LogoData::Load(filename, &header), &header)
	{ }

	int getWidth() { return header.w; }
	int getHeight() { return header.h; }
	int getX() { return header.imgx; }
	int getY() { return header.imgy; }
	int getImgWidth() { return header.imgw; }
	int getImgHeight() { return header.imgh; }
	int getServiceId() { return header.serviceId; }
	void setServiceId(int serviceId) { header.serviceId = serviceId; }
	const char* getName() { return header.name; }
	void setName(const char* name) { strcpy_s(header.name, name); }

	void getImage(uint8_t* rgb, int stride, uint8_t bg) {
		const float *logoAY = logo.GetA(PLANAR_Y);
		const float *logoBY = logo.GetB(PLANAR_Y);
		const float *logoAU = logo.GetA(PLANAR_U);
		const float *logoBU = logo.GetB(PLANAR_U);
		const float *logoAV = logo.GetA(PLANAR_V);
		const float *logoBV = logo.GetB(PLANAR_V);

		for (int y = 0; y < header.h; ++y) {
			uint8_t* line = rgb + y * stride;
			for (int x = 0; x < header.w; ++x) {
				float aY = logoAY[x + y * header.w];
				float bY = logoBY[x + y * header.w];
				float aU = logoAU[x + y * header.w];
				float bU = logoBU[x + y * header.w];
				float aV = logoAV[x + y * header.w];
				float bV = logoBV[x + y * header.w];
				float fY = ((bg - bY * 255) / aY);
				float fU = ((bg - bU * 255) / aU);
				float fV = ((bg - bV * 255) / aV);
				// B
				line[x * 3 + 0] = (uint8_t)std::max(0.0f, std::min(255.0f, (fY + 1.8556f * fU + 0.5f)));
				// G
				line[x * 3 + 1] = (uint8_t)std::max(0.0f, std::min(255.0f, (fY - 0.187324f * fU - 0.468124f * fV + 0.5f)));
				// R
				line[x * 3 + 2] = (uint8_t)std::max(0.0f, std::min(255.0f, (fY + 1.5748f * fV + 0.5f)));
			}
		}
	}

	bool save(const char* filename) {
		try {
			logo.Save(filename, &header);
			return true;
		}
		catch (Exception& exception) {
			ctx.setError(exception);
		}
		return false;
	}
};

} // namespace logo

extern "C" __declspec(dllexport) void* LogoFile_Create(AMTContext* ctx, const char* filepath) {
	try {
		return new logo::GUILogoFile(*ctx, filepath);
	}
	catch (Exception& exception) {
		ctx->setError(exception);
	}
	return nullptr;
}
extern "C" __declspec(dllexport) void LogoFile_Delete(logo::GUILogoFile* ptr) { delete ptr; }
extern "C" __declspec(dllexport) int LogoFile_GetWidth(logo::GUILogoFile* ptr) { return ptr->getWidth(); }
extern "C" __declspec(dllexport) int LogoFile_GetHeight(logo::GUILogoFile* ptr) { return ptr->getHeight(); }
extern "C" __declspec(dllexport) int LogoFile_GetX(logo::GUILogoFile* ptr) { return ptr->getX(); }
extern "C" __declspec(dllexport) int LogoFile_GetY(logo::GUILogoFile* ptr) { return ptr->getY(); }
extern "C" __declspec(dllexport) int LogoFile_GetImgWidth(logo::GUILogoFile* ptr) { return ptr->getImgWidth(); }
extern "C" __declspec(dllexport) int LogoFile_GetImgHeight(logo::GUILogoFile* ptr) { return ptr->getImgHeight(); }
extern "C" __declspec(dllexport) int LogoFile_GetServiceId(logo::GUILogoFile* ptr) { return ptr->getServiceId(); }
extern "C" __declspec(dllexport) void LogoFile_SetServiceId(logo::GUILogoFile* ptr, int serviceId) { ptr->setServiceId(serviceId); }
extern "C" __declspec(dllexport) const char* LogoFile_GetName(logo::GUILogoFile* ptr) { return ptr->getName(); }
extern "C" __declspec(dllexport) void LogoFile_SetName(logo::GUILogoFile* ptr, const char* name) { ptr->setName(name); }
extern "C" __declspec(dllexport) void LogoFile_GetImage(logo::GUILogoFile* ptr, uint8_t* rgb, int stride, uint8_t bg) { ptr->getImage(rgb, stride, bg); }
extern "C" __declspec(dllexport) bool LogoFile_Save(logo::GUILogoFile* ptr, const char* filename) { return ptr->save(filename); }
