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

	// OnFrameDecodedで直前にデコードされたフレーム
	// まだデコードしてない場合は-1
	int lastDecodeFrame;

	int64_t fileSize;

	Frame prevframe;
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
		if (avcodec_open2(codecCtx(), pCodec, NULL) != 0) {
			THROW(FormatException, "avcodec_open2 failed");
		}
	}

	void init(AVFrame* frame)
	{
		if (swsctx) {
			sws_freeContext(swsctx);
			swsctx = nullptr;
		}
		width = frame->width;
		height = frame->height;
		swsctx = sws_getCachedContext(NULL, width, height,
			(AVPixelFormat)frame->format, width, height,
			AV_PIX_FMT_BGR24, 0, 0, 0, 0);
	}

	void ConvertToRGB(uint8_t* rgb, AVFrame* frame)
	{
		uint8_t * outData[1] = { rgb };
		int outLinesize[1] = { width * 3 };
		sws_scale(swsctx, frame->data, frame->linesize, 0, height, outData, outLinesize);
	}

	bool DecodeOneFrame(int64_t startpos) {
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
						if (frame()->width != width || frame()->height != height) {
							init(frame());
						}
						prevframe = frame;
						ok = true;
					}
				}
			}
			int64_t packetpos = packet.pos;
			av_packet_unref(&packet);
			if (ok) {
				break;
			}
			if (packetpos != -1) {
				if (packetpos - startpos > 50 * 1024 * 1024) {
					// 50MB読んでもデコードできなかったら終了
					return false;
				}
			}
		}
		return ok;
	}

public:
	GUIMediaFile(AMTContext& ctx, const char* filepath)
		: AMTObject(ctx)
		, inputCtx(filepath)
		, width(-1)
		, height(-1)
		, swsctx(nullptr)
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
		DecodeOneFrame(0);
	}

	~GUIMediaFile() {
		sws_freeContext(swsctx);
		swsctx = nullptr;
	}

	void getFrame(uint8_t* rgb, int width, int height) {
		if (this->width == width && this->height && height) {
			ConvertToRGB(rgb, prevframe());
		}
	}

	bool decodeFrame(float pos, int* pwidth, int* pheight) {
		ctx.setError(Exception());
		try {
			int64_t fileOffset = int64_t(fileSize * pos);
			if (av_seek_frame(inputCtx(), -1, fileOffset, AVSEEK_FLAG_BYTE) < 0) {
				THROW(FormatException, "av_seek_frame failed");
			}
			lastDecodeFrame = -1;
			MakeCodecContext();
			if (DecodeOneFrame(fileOffset)) {
				*pwidth = width;
				*pheight = height;
			}
			return true;
		}
		catch (const Exception& exception) {
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
	catch (const Exception& exception) {
		ctx->setError(exception);
	}
	return nullptr;
}
extern "C" __declspec(dllexport) void MediaFile_Delete(av::GUIMediaFile* ptr) { delete ptr; }
extern "C" __declspec(dllexport) int MediaFile_DecodeFrame(
	av::GUIMediaFile* ptr, float pos, int* pwidth, int* pheight) { return ptr->decodeFrame(pos, pwidth, pheight); }
extern "C" __declspec(dllexport) void MediaFile_GetFrame(
	av::GUIMediaFile* ptr, uint8_t* rgb, int width, int height) { ptr->getFrame(rgb, width, height); }

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

		int widthUV = (header.w >> header.logUVx);

		float bgY = 0.2126f * bg + 0.7152f * bg + 0.0722f * bg;
		float bgU = 0.5389f * (bg - bgY);
		float bgV = 0.6350f * (bg - bgY);

		for (int y = 0; y < header.h; ++y) {
			uint8_t* line = rgb + y * stride;
			for (int x = 0; x < header.w; ++x) {
				int UVx = (x >> header.logUVx);
				int UVy = (y >> header.logUVy);
				float aY = logoAY[x + y * header.w];
				float bY = logoBY[x + y * header.w];
				float aU = logoAU[UVx + UVy * widthUV];
				float bU = logoBU[UVx + UVy * widthUV];
				float aV = logoAV[UVx + UVy * widthUV];
				float bV = logoBV[UVx + UVy * widthUV];
				float fY = ((bgY - bY * 255) / aY);
				float fU = ((bgU - bU * 255) / aU);
				float fV = ((bgV - bV * 255) / aV);
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
		catch (const Exception& exception) {
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
	catch (const Exception& exception) {
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
extern "C" __declspec(dllexport) int LogoFile_Save(logo::GUILogoFile* ptr, const char* filename) { return ptr->save(filename); }
