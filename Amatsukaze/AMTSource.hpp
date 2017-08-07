#pragma once

#include <windows.h>
#include "avisynth.h"

#include <memory>

#include "Tree.hpp"
#include "List.hpp"
#include "Transcode.hpp"

namespace av {

struct FakeAudioSample {

  enum {
    MAGIC = 0xFACE0D10,
    VERSION = 1
  };

  int32_t magic;
  int32_t version;
  int64_t index;
};

class AMTSource : public IClip, AMTObject
{
  const std::vector<FilterSourceFrame>& frames;

  InputContext inputCtx;
  CodecContext codecCtx;

  AVStream *videoStream;

  struct CacheFrame {
    PVideoFrame data;
    TreeNode<int, CacheFrame*> treeNode;
    ListNode<CacheFrame*> listNode;
  };

  Tree<int, CacheFrame*> frameCache;
  List<CacheFrame*> recentAccessed;

  VideoInfo vi;

  // AMT -> ffmpeg 変換
  int64_t ptsDiff;

  bool initialized;

  int seekDistance;

  // OnFrameDecodedで直前にデコードされたフレーム
  // lastDecodeFrameは必ずキャッシュにある
  // まだデコードしてない場合は-1
  int lastDecodeFrame;

  // codecCtxが直前にデコードしたフレーム番号
  // まだデコードしてない場合はnullptr
  std::unique_ptr<Frame> prevFrame;

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

  void MakeVideoInfo(const VideoFormat& format) {
    vi.width = format.width;
    vi.height = format.height;
    vi.SetFPS(format.frameRateNum, format.frameRateDenom);
    vi.num_frames = int(frames.size());

    // ビット深度は取得してないのでフレームをデコードして取得する
    //vi.pixel_type = VideoInfo::CS_YV12;
    
    vi.audio_samples_per_second = 1000;
    vi.sample_type = SAMPLE_INT32;
    vi.num_audio_samples = int64_t(std::round(
      (double)vi.audio_samples_per_second * vi.num_frames * format.frameRateDenom / format.frameRateNum));
    vi.nchannels = (sizeof(FakeAudioSample) + 3) / 4;
  }

  int GetKeyframeNumber(int n) {
    return frames[n].keyFrame;
  }

  int64_t GetFramePTS(int n) {
    return frames[n].framePTS;
  }

  void ResetDecoder() {
    lastDecodeFrame = -1;
    prevFrame = nullptr;
    MakeCodecContext();
  }

  template <typename T, int step>
  void Copy(T* dst, const T* top, const T* bottom, int w, int h, int dpitch, int tpitch, int bpitch)
  {
    for (int y = 0; y < h; y += 2) {
      uint8_t* dst0 = dst + dpitch * (y + 0);
      uint8_t* dst1 = dst + dpitch * (y + 1);
      uint8_t* src0 = top + tpitch * (y >> 1);
      uint8_t* src1 = bottom + bpitch * (y >> 1);
      for (int x = 0; x < w; ++x) {
        dst0[x] = src0[x * step];
        dst1[x] = src1[x * step];
      }
    }
  }

  template <typename T>
  void MergeField(PVideoFrame& dst, AVFrame* top, AVFrame* bottom) {

    T* srctY = (T*)top->data[0];
    T* srctU = (T*)top->data[1];
    T* srctV = (top->format != AV_PIX_FMT_NV12) ? (T*)top->data[2] : ((T*)top->data[1] + 1);
    T* srcbY = (T*)bottom->data[0];
    T* srcbU = (T*)bottom->data[1];
    T* srcbV = (top->format != AV_PIX_FMT_NV12) ? (T*)bottom->data[2] : ((T*)bottom->data[1] + 1);
    T* dstY = (T*)dst->GetWritePtr(PLANAR_Y);
    T* dstU = (T*)dst->GetWritePtr(PLANAR_U);
    T* dstV = (T*)dst->GetWritePtr(PLANAR_V);

    int srctPitchY = top->linesize[0];
    int srctPitchUV = top->linesize[1];
    int srcbPitchY = bottom->linesize[0];
    int srcbPitchUV = bottom->linesize[1];
    int dstPitchY = dst->GetPitch(PLANAR_Y);
    int dstPitchUV = dst->GetPitch(PLANAR_U);

    Copy<T, 1>(dstY, srctY, srcbY, vi.width, vi.height, dstPitchY, srctPitchY, srcbPitchY);

    int witchUV = vi.width >> desc->log2_chroma_w;
    int heightUV = vi.height >> desc->log2_chroma_h;
    if (top->format != AV_PIX_FMT_NV12) {
      Copy<T, 1>(dstU, srctU, srcbU, witchUV, heightUV, dstPitchUV, srctPitchUV, srcbPitchUV);
      Copy<T, 1>(dstV, srctV, srcbV, witchUV, viheightUV, dstPitchUV, srctPitchUV, srcbPitchUV);
    }
    else {
      Copy<T, 2>(dstU, srctU, srcbU, witchUV, heightUV, dstPitchUV, srctPitchUV, srcbPitchUV);
      Copy<T, 2>(dstV, srctV, srcbV, witchUV, viheightUV, dstPitchUV, srctPitchUV, srcbPitchUV);
    }
  }

  PVideoFrame MakeFrame(AVFrame* top, AVFrame* bottom, IScriptEnvironment* env) {
    PVideoFrame ret = env->NewVideoFrame(vi);
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get((AVPixelFormat)(top->format));
    
    if (desc->comp[0].depth > 8) {
      MergeField<uint16_t>(ret, top, bottom);
    }
    else {
      MergeField<uint8_t>(ret, top, bottom);
    }
  }

  void PutFrame(int n, const PVideoFrame& frame) {
    CacheFrame* pcache = new CacheFrame();
    pcache->data = frame;
    pcache->treeNode.key = n;
    pcache->treeNode.value = pcache;
    pcache->listNode.value = pcache;
    frameCache.insert(&pcache->treeNode);
    recentAccessed.push_front(&pcache->listNode);

    if (recentAccessed.size() > seekDistance * 2) {
      // キャッシュから溢れたら削除
      CacheFrame* pdel = recentAccessed.back().value;
      frameCache.erase(frameCache.it(&pdel->treeNode));
      recentAccessed.erase(recentAccessed.it(&pdel->listNode));
      delete pdel;
    }
  }

  void OnFrameDecoded(Frame& frame, IScriptEnvironment* env) {

    if (initialized == false) {
      // 初期化
      double cycle = (int64_t(1) << 32);
      double cycleDiff = std::round((double)(frame()->pts - frames[0].framePTS) / cycle);
      ptsDiff = int64_t(cycleDiff * cycle);

      // ビット深度取得
      const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get((AVPixelFormat)(frame()->format));
      switch (desc->comp[0].depth) {
      case 8:
        vi.pixel_type = VideoInfo::CS_YV12;
        break;
      case 10:
        vi.pixel_type = VideoInfo::CS_YUV420P10;
        break;
      case 12:
        vi.pixel_type = VideoInfo::CS_YUV420P12;
        break;
      default:
        env->ThrowError("対応していないビット深度です");
        break;
      }

      initialized = true;
    }

    int64_t pts = frame()->pts - ptsDiff;
    auto it = std::lower_bound(frames.begin(), frames.end(), pts, [](FilterSourceFrame& e, int64_t pts) {
      return e.framePTS < pts;
    });

    if (it == frames.end()) {
      // 最後より後ろだった
      lastDecodeFrame = vi.num_frames;
      return;
    }

    if (it->framePTS != pts) {
      // 一致するフレームがない
      ctx.incrementCounter("incident");
      ctx.warn("Unknown PTS frame %lld", pts);
      return;
    }

    int frameIndex = int(it - frames.begin());
    auto cacheit = frameCache.find(frameIndex);

    if (it->halfDelay) {
      // ディレイを適用させる
      if (cacheit != frameCache.end()) {
        // すでにキャッシュにある
        UpdateAccessed(cacheit->value);
        lastDecodeFrame = frameIndex;
      }
      else if (prevFrame != nullptr) {
        PutFrame(frameIndex, MakeFrame((*prevFrame)(), frame(), env));
        lastDecodeFrame = frameIndex;
      }

      // 次のフレームも同じフレームを参照してたらそれも出力
      auto next = it + 1;
      if (next != frames.end() && next->framePTS == it->framePTS) {
        auto cachenext = frameCache.find(frameIndex + 1);
        if (cachenext != frameCache.end()) {
          // すでにキャッシュにある
          UpdateAccessed(cachenext->value);
          lastDecodeFrame = frameIndex + 1;
        }
        else {
          PutFrame(frameIndex + 1, MakeFrame(frame(), frame(), env));
          lastDecodeFrame = frameIndex + 1;
        }
      }
    }
    else {
      // そのまま
      if (cacheit != frameCache.end()) {
        // すでにキャッシュにある
        UpdateAccessed(cacheit->value);
        lastDecodeFrame = frameIndex;
      }
      else {
        PutFrame(frameIndex, MakeFrame(frame(), frame(), env));
        lastDecodeFrame = frameIndex;
      }
    }

    prevFrame = std::unique_ptr<Frame>(new Frame(frame));
  }

  void UpdateAccessed(CacheFrame* frame) {
    recentAccessed.erase(recentAccessed.it(&frame->listNode));
    recentAccessed.push_front(&frame->listNode);
  }

  PVideoFrame ForceGetFrame(int n, IScriptEnvironment* env) {
    if (frameCache.size() == 0) {
      return env->NewVideoFrame(vi);
    }
    auto lb = frameCache.lower_bound(n);
    if (lb->key != n && lb != frameCache.begin()) {
      --lb;
    }
    UpdateAccessed(lb->value);
    return lb->value->data;
  }

  void DecodeLoop(int goal, IScriptEnvironment* env) {
    Frame frame(-1);
    AVPacket packet = AVPacket();
    while (av_read_frame(inputCtx(), &packet) == 0) {
      if (packet.stream_index == videoStream->index) {
        if (avcodec_send_packet(codecCtx(), &packet) != 0) {
          THROW(FormatException, "avcodec_send_packet failed");
        }
        while (avcodec_receive_frame(codecCtx(), frame()) == 0) {
          OnFrameDecoded(frame, env);
        }
      }
      av_packet_unref(&packet);
      if (lastDecodeFrame >= goal) {
        break;
      }
    }
  }

public:
  AMTSource(AMTContext& ctx,
    const std::string& srcpath,
    const VideoFormat& format,
    const std::vector<FilterSourceFrame>& frames,
    IScriptEnvironment* env)
    : AMTObject(ctx)
    , frames(frames)
    , inputCtx(srcpath)
    , vi()
    , ptsDiff()
    , seekDistance(10)
    , initialized(false)
    , lastDecodeFrame(-1)
  {
    MakeVideoInfo(format);

    if (avformat_find_stream_info(inputCtx(), NULL) < 0) {
      THROW(FormatException, "avformat_find_stream_info failed");
    }
    videoStream = GetVideoStream(inputCtx());
    if (videoStream == NULL) {
      THROW(FormatException, "Could not find video stream ...");
    }

    // 最初のフレームをデコードしてPTS差を求めておく
    ResetDecoder();
    DecodeLoop(0, env);
  }

  PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env)
  {
    // キャッシュにあれば返す
    auto it = frameCache.find(n);
    if (it != frameCache.end()) {
      UpdateAccessed(it->value);
      return it->value->data;
    }

    // キャッシュにないのでデコードする
    if (lastDecodeFrame != -1 && n > lastDecodeFrame && n < lastDecodeFrame + seekDistance) {
      // 前にすすめる
      DecodeLoop(n, env);
    }
    else {
      // シークしてデコードする
      int keyNum = GetKeyframeNumber(n);
      for (int i = 0; i < 3; ++i) {
        if (keyNum > 0) {
          int64_t keyFramePts = GetFramePTS(keyNum);
          if (av_seek_frame(inputCtx(), videoStream->index, keyFramePts, 0) < 0) {
            THROW(FormatException, "av_seek_frame failed");
          }
        }
        else {
          // 0だったらファイルの先頭に行く
          if (av_seek_frame(inputCtx(), videoStream->index, 0, AVSEEK_FLAG_BYTE) < 0) {
            THROW(FormatException, "av_seek_frame failed");
          }
        }
        ResetDecoder();
        DecodeLoop(n, env);
        if (frameCache.find(n) != frameCache.end() || keyNum <= 0) {
          seekDistance = std::max(seekDistance, n - keyNum);
          break;
        }
        keyNum = GetKeyframeNumber(keyNum - 1);
      }
    }

    return ForceGetFrame(n, env);
  }

  void __stdcall GetAudio(void* buf, __int64 start, __int64 count, IScriptEnvironment* env)
  {
    FakeAudioSample* ptr = reinterpret_cast<FakeAudioSample*>(buf);
    for (__int64 i = 0; i < count; ++i) {
      FakeAudioSample t = { FakeAudioSample::MAGIC, FakeAudioSample::VERSION, start + i };
      ptr[i] = t;
    }
  }

  const VideoInfo& __stdcall GetVideoInfo() { return vi; }
  bool __stdcall GetParity(int n) { return 1; }
  int __stdcall SetCacheHints(int cachehints, int frame_range) { return 0; };
};

} // namespace av {
