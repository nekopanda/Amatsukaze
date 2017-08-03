#pragma once

#include <windows.h>
#include "avisynth.h"

#include <memory>

#include "Transcode.hpp"

namespace av {

  class AMTSource : public IClip
  {
    InputContext inputCtx;
    CodecContext codecCtx;
  public:
    AMTSource(const std::string& srcpath, IScriptEnvironment* env)
      : inputCtx(srcpath)
    {
      if (avformat_find_stream_info(inputCtx(), NULL) < 0) {
        THROW(FormatException, "avformat_find_stream_info failed");
      }
      AVStream *videoStream = GetVideoStream(inputCtx());
      if (videoStream == NULL) {
        THROW(FormatException, "Could not find video stream ...");
      }
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

    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env)
    {
      // TODO:
    }

    void __stdcall GetAudio(void* buf, __int64 start, __int64 count, IScriptEnvironment* env)
    {
      // TODO:
    }

    const VideoInfo& __stdcall GetVideoInfo()
    {
      // TODO:
    }

    bool __stdcall GetParity(int n) { return 1; }
    int __stdcall SetCacheHints(int cachehints, int frame_range) { return 0; };
  };

} // namespace av {
