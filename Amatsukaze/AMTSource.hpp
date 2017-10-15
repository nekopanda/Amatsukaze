#pragma once

#include <windows.h>

#include "avisynth.h"
#pragma comment(lib, "avisynth.lib")

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
	const std::vector<FilterAudioFrame>& audioFrames;

  InputContext inputCtx;
  CodecContext codecCtx;

  AVStream *videoStream;

  std::unique_ptr<StreamReformInfo> storage;

  struct CacheFrame {
    PVideoFrame data;
    TreeNode<int, CacheFrame*> treeNode;
    ListNode<CacheFrame*> listNode;
  };

  Tree<int, CacheFrame*> frameCache;
  List<CacheFrame*> recentAccessed;

  VideoInfo vi;

	File waveFile;

  bool initialized;

  int seekDistance;

  // OnFrameDecodedで直前にデコードされたフレーム
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

  void MakeVideoInfo(const OutVideoFormat& format) {
    vi.width = format.videoFormat.width;
    vi.height = format.videoFormat.height;
    vi.SetFPS(format.videoFormat.frameRateNum, format.videoFormat.frameRateDenom);
    vi.num_frames = int(frames.size());

    // ビット深度は取得してないのでフレームをデコードして取得する
    //vi.pixel_type = VideoInfo::CS_YV12;
    
		if (audioFrames.size() > 0) {
			int samplesPerFrame = audioFrames[0].waveLength / 4; // 16bitステレオ前提
			vi.audio_samples_per_second = format.audioFormat[0].sampleRate;
			vi.sample_type = SAMPLE_INT16;
			vi.num_audio_samples = samplesPerFrame * audioFrames.size();
			vi.nchannels = 2;
		}
		else {
			// No audio
			vi.audio_samples_per_second = 0;
			vi.num_audio_samples = 0;
			vi.nchannels = 0;
		}
  }

  void ResetDecoder() {
    lastDecodeFrame = -1;
    prevFrame = nullptr;
    //if (codecCtx() == nullptr) {
      MakeCodecContext();
    //}
    //avcodec_flush_buffers(codecCtx());
  }

  template <typename T, int step>
  void Copy(T* dst, const T* top, const T* bottom, int w, int h, int dpitch, int tpitch, int bpitch)
  {
    for (int y = 0; y < h; y += 2) {
      T* dst0 = dst + dpitch * (y + 0);
      T* dst1 = dst + dpitch * (y + 1);
      const T* src0 = top + tpitch * (y + 0);
      const T* src1 = bottom + bpitch * (y + 1);
      for (int x = 0; x < w; ++x) {
        dst0[x] = src0[x * step];
        dst1[x] = src1[x * step];
      }
    }
  }

  template <typename T>
  void MergeField(PVideoFrame& dst, AVFrame* top, AVFrame* bottom) {
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get((AVPixelFormat)(top->format));

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

    int widthUV = vi.width >> desc->log2_chroma_w;
    int heightUV = vi.height >> desc->log2_chroma_h;
    if (top->format != AV_PIX_FMT_NV12) {
      Copy<T, 1>(dstU, srctU, srcbU, widthUV, heightUV, dstPitchUV, srctPitchUV, srcbPitchUV);
      Copy<T, 1>(dstV, srctV, srcbV, widthUV, heightUV, dstPitchUV, srctPitchUV, srcbPitchUV);
    }
    else {
      Copy<T, 2>(dstU, srctU, srcbU, widthUV, heightUV, dstPitchUV, srctPitchUV, srcbPitchUV);
      Copy<T, 2>(dstV, srctV, srcbV, widthUV, heightUV, dstPitchUV, srctPitchUV, srcbPitchUV);
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

    return ret;
  }

  void PutFrame(int n, const PVideoFrame& frame) {
    CacheFrame* pcache = new CacheFrame();
    pcache->data = frame;
    pcache->treeNode.key = n;
    pcache->treeNode.value = pcache;
    pcache->listNode.value = pcache;
    frameCache.insert(&pcache->treeNode);
    recentAccessed.push_front(&pcache->listNode);

    if (recentAccessed.size() > seekDistance * 3 / 2) {
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

    // ffmpegのpts wrapの仕方が謎なので下位33bitのみを見る
    //（26時間以上ある動画だと重複する可能性はあるが無視）
    int64_t pts = frame()->pts & ((int64_t(1) << 33) - 1);
    auto it = std::lower_bound(frames.begin(), frames.end(), pts, [](const FilterSourceFrame& e, int64_t pts) {
      return e.framePTS < pts;
    });

    if (it == frames.begin() && it->framePTS != pts) {
      // 小さすぎた場合は1周分追加して見る
      pts += ((int64_t(1) << 33) - 1);
      it = std::lower_bound(frames.begin(), frames.end(), pts, [](const FilterSourceFrame& e, int64_t pts) {
        return e.framePTS < pts;
      });
    }

    if (it == frames.end()) {
      // 最後より後ろだった
      lastDecodeFrame = vi.num_frames;
      prevFrame = nullptr; // 連続でなくなる場合はnullリセット
      return;
    }

    if (it->framePTS != pts) {
      // 一致するフレームがない
      ctx.incrementCounter("incident");
      ctx.warn("Unknown PTS frame %lld", pts);
      prevFrame = nullptr; // 連続でなくなる場合はnullリセット
      return;
    }

    int frameIndex = int(it - frames.begin());
    auto cacheit = frameCache.find(frameIndex);

    lastDecodeFrame = frameIndex;

    if (it->halfDelay) {
      // ディレイを適用させる
      if (cacheit != frameCache.end()) {
        // すでにキャッシュにある
        UpdateAccessed(cacheit->value);
      }
      else if (prevFrame != nullptr) {
        PutFrame(frameIndex, MakeFrame((*prevFrame)(), frame(), env));
      }

      // 次のフレームも同じフレームを参照してたらそれも出力
      auto next = it + 1;
      if (next != frames.end() && next->framePTS == it->framePTS) {
        auto cachenext = frameCache.find(frameIndex + 1);
        if (cachenext != frameCache.end()) {
          // すでにキャッシュにある
          UpdateAccessed(cachenext->value);
        }
        else {
          PutFrame(frameIndex + 1, MakeFrame(frame(), frame(), env));
        }
        lastDecodeFrame = frameIndex + 1;
      }
    }
    else {
      // そのまま
      if (cacheit != frameCache.end()) {
        // すでにキャッシュにある
        UpdateAccessed(cacheit->value);
      }
      else {
        PutFrame(frameIndex, MakeFrame(frame(), frame(), env));
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
    Frame frame;
    AVPacket packet = AVPacket();
    while (av_read_frame(inputCtx(), &packet) == 0) {
      if (packet.stream_index == videoStream->index) {
        if (avcodec_send_packet(codecCtx(), &packet) != 0) {
          THROW(FormatException, "avcodec_send_packet failed");
        }
        while (avcodec_receive_frame(codecCtx(), frame()) == 0) {
          // 最初はIフレームまでスキップ
          if (lastDecodeFrame != -1 || frame()->key_frame) {
            OnFrameDecoded(frame, env);
          }
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
		const std::string& audiopath,
		const OutVideoFormat& format,
		const std::vector<FilterSourceFrame>& frames,
		const std::vector<FilterAudioFrame>& audioFrames,
    IScriptEnvironment* env)
    : AMTObject(ctx)
    , frames(frames)
		, audioFrames(audioFrames)
    , inputCtx(srcpath)
    , vi()
		, waveFile(audiopath, "rb")
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

    ResetDecoder();
    DecodeLoop(0, env);
  }

  ~AMTSource() {
    // キャッシュを削除
    while (recentAccessed.size() > 0) {
      CacheFrame* pdel = recentAccessed.back().value;
      frameCache.erase(frameCache.it(&pdel->treeNode));
      recentAccessed.erase(recentAccessed.it(&pdel->listNode));
      delete pdel;
    }
  }

  void TransferStreamInfo(std::unique_ptr<StreamReformInfo>&& streamInfo) {
    storage = std::move(streamInfo);
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
      int keyNum = frames[n].keyFrame;
      for (int i = 0; i < 3; ++i) {
        int64_t fileOffset = frames[keyNum].fileOffset / 188 * 188;
        if (av_seek_frame(inputCtx(), -1, fileOffset, AVSEEK_FLAG_BYTE) < 0) {
          THROW(FormatException, "av_seek_frame failed");
        }
        ResetDecoder();
        DecodeLoop(n, env);
        if (frameCache.find(n) != frameCache.end()) {
          // デコード成功
          seekDistance = std::max(seekDistance, n - keyNum);
          break;
        }
        if (keyNum <= 0) {
          // これ以上戻れない
          break;
        }
        if (lastDecodeFrame >= 0 && lastDecodeFrame < n) {
          // データが足りなくてゴールに到達できなかった
          break;
        }
        keyNum -= std::max(5, keyNum - frames[keyNum - 1].keyFrame);
      }
    }

    return ForceGetFrame(n, env);
  }

  void __stdcall GetAudio(void* buf, __int64 start, __int64 count, IScriptEnvironment* env)
  {
		if (audioFrames.size() == 0) return;

		const int sampleBytes = 4; // 16bitステレオ前提
		int samplesPerFrame = audioFrames[0].waveLength / sampleBytes;
		uint8_t* ptr = (uint8_t*)buf;
		for(int frameIndex = start / samplesPerFrame, frameOffset = start % samplesPerFrame;
			count > 0 && frameIndex < (int)audioFrames.size();
			++frameIndex, frameOffset = 0)
		{
			waveFile.seek(audioFrames[frameIndex].waveOffset + frameOffset, SEEK_SET);
			int readBytes = std::min<int>(audioFrames[frameIndex].waveLength, count * sampleBytes);
			waveFile.read(MemoryChunk(ptr, readBytes));
			ptr += readBytes;
			count -= readBytes;
		}
		if (count > 0) {
			// ファイルの終わりまで到達したら残りはゼロで埋める
			memset(ptr, 0, count * sampleBytes);
		}
  }

  const VideoInfo& __stdcall GetVideoInfo() { return vi; }
  bool __stdcall GetParity(int n) { return 1; }
	int __stdcall SetCacheHints(int cachehints, int frame_range)
	{
		if (cachehints == CACHE_GET_MTMODE) return MT_SERIALIZED;
		return 0;
	};
};

class AMTSourceIndex : public TsSplitter {
public:
  AMTSourceIndex(AMTContext& ctx, const std::string& videofile)
    : TsSplitter(ctx)
    , videofile(videofile)
    , videoFileCount_(0)
    , audioFileSize_(0)
  {
  }

  bool tryReadCache() {
		std::string audioname = videofile + ".amaud";
		if (File::exists(audioname.c_str()) == false) {
			return false;
		}
    std::string indexname = videofile + ".ami";
    if (File::exists(indexname.c_str()) == false) {
      return false;
    }
    File file(indexname.c_str(), "rb");
    uint32_t magic = file.readValue<uint32_t>();
    uint32_t version = file.readValue<uint32_t>();
    if (magic != MAGIC || version != VERSION) {
      return false;
    }
    videoFileCount_ = file.readValue<int>();
    videoFrameList_ = file.readArray<FileVideoFrameInfo>();
    audioFrameList_ = file.readArray<FileAudioFrameInfo>();
    streamEventList_ = file.readArray<StreamEvent>();
    return true;
  }

  void writeCache() {
    std::string indexname = videofile + ".ami";
    File file(indexname.c_str(), "wb");
    file.writeValue(uint32_t(MAGIC));
    file.writeValue(uint32_t(VERSION));
    file.writeValue(videoFileCount_);
    file.writeArray(videoFrameList_);
    file.writeArray(audioFrameList_);
    file.writeArray(streamEventList_);
  }

  void split() {
		audioFile = std::unique_ptr<File>(new File(videofile + ".amaud", "wb"));

    readAll();

		audioFile = nullptr;

    // fileOffsetを5フレーム分遅らせる
    for (int i = int(videoFrameList_.size()) - 1; i >= 0; --i) {
      if (i < 5) {
        videoFrameList_[i].fileOffset = 0;
      }
      else {
        videoFrameList_[i].fileOffset = videoFrameList_[i - 5].fileOffset;
      }
    }
  }

  std::unique_ptr<StreamReformInfo> getStreamInfo() {
    return std::unique_ptr<StreamReformInfo>(new StreamReformInfo(
      ctx, videoFileCount_, videoFrameList_, audioFrameList_, streamEventList_));
  }

protected:
  enum {
    MAGIC = 0xB16B00B5,
    VERSION = 1
  };

  std::string videofile;
  int videoFileCount_;
	int64_t audioFileSize_;
	int64_t waveFileSize_;

  int64_t fileOffset_;

	std::unique_ptr<File> audioFile;

  // データ
  std::vector<FileVideoFrameInfo> videoFrameList_;
  std::vector<FileAudioFrameInfo> audioFrameList_;
  std::vector<StreamEvent> streamEventList_;

  void readAll() {
    enum { BUFSIZE = 128 * 1024 };
    auto buffer_ptr = std::unique_ptr<uint8_t[]>(new uint8_t[BUFSIZE]);
    MemoryChunk buffer(buffer_ptr.get(), BUFSIZE);
    File srcfile(videofile, "rb");
    fileOffset_ = 0;
    size_t readBytes;
    do {
      readBytes = srcfile.read(buffer);
      inputTsData(MemoryChunk(buffer.data, readBytes));
      fileOffset_ += readBytes;
    } while (readBytes == buffer.length);
  }

  // TsSplitter仮想関数 //

  virtual void onVideoPesPacket(
    int64_t clock,
    const std::vector<VideoFrameInfo>& frames,
    PESPacket packet)
  {
    for (const VideoFrameInfo& frame : frames) {
      FileVideoFrameInfo info = frame;
      info.fileOffset = fileOffset_;
      videoFrameList_.push_back(info);
    }
  }

  virtual void onVideoFormatChanged(VideoFormat fmt) {
    ctx.info("[映像フォーマット変更]");
    if (fmt.fixedFrameRate) {
      ctx.info("サイズ: %dx%d FPS: %d/%d", fmt.width, fmt.height, fmt.frameRateNum, fmt.frameRateDenom);
    }
    else {
      ctx.info("サイズ: %dx%d FPS: VFR", fmt.width, fmt.height);
    }

    // 出力ファイルを変更
    videoFileCount_++;

    StreamEvent ev = StreamEvent();
    ev.type = VIDEO_FORMAT_CHANGED;
    ev.frameIdx = (int)videoFrameList_.size();
    streamEventList_.push_back(ev);
  }

  virtual void onAudioPesPacket(
    int audioIdx,
    int64_t clock,
    const std::vector<AudioFrameData>& frames,
    PESPacket packet)
  {
    for (const AudioFrameData& frame : frames) {
      FileAudioFrameInfo info = frame;
      info.audioIdx = audioIdx;
      info.codedDataSize = frame.codedDataSize;
			info.waveDataSize = frame.decodedDataSize;
      info.fileOffset = audioFileSize_;
			info.waveOffset = waveFileSize_;
			audioFileSize_ += frame.codedDataSize;
			waveFileSize_ += frame.decodedDataSize;
			audioFile->write(MemoryChunk((uint8_t*)frame.decodedData, frame.decodedDataSize));
      audioFrameList_.push_back(info);
    }
  }

  virtual void onAudioFormatChanged(int audioIdx, AudioFormat fmt) {
    ctx.info("[音声%dフォーマット変更]", audioIdx);
    ctx.info("チャンネル: %s サンプルレート: %d",
      getAudioChannelString(fmt.channels), fmt.sampleRate);

    StreamEvent ev = StreamEvent();
    ev.type = AUDIO_FORMAT_CHANGED;
    ev.audioIdx = audioIdx;
    ev.frameIdx = (int)audioFrameList_.size();
    streamEventList_.push_back(ev);
  }

  // TsPacketSelectorHandler仮想関数 //

  virtual void onPidTableChanged(const PMTESInfo video, const std::vector<PMTESInfo>& audio) {
    // ベースクラスの処理
    TsSplitter::onPidTableChanged(video, audio);

    ASSERT(audio.size() > 0);

    StreamEvent ev = StreamEvent();
    ev.type = PID_TABLE_CHANGED;
    ev.numAudio = (int)audio.size();
    ev.frameIdx = (int)videoFrameList_.size();
    streamEventList_.push_back(ev);
  }
};

AMTContext* g_ctx_for_plugin_filter = nullptr;

const char* GetInternalAMTSouceName() {
  return "GetInternalAMTSource";
}

AVSValue CreateAMTSource(AVSValue args, void* user_data, IScriptEnvironment* env) {
  
  if (env->FunctionExists(GetInternalAMTSouceName())) {
    return env->Invoke(GetInternalAMTSouceName(), AVSValue(0, 0));
  }

  if (g_ctx_for_plugin_filter == nullptr) {
    g_ctx_for_plugin_filter = new AMTContext();
  }

  std::string filename = args[0].AsString();
  AMTSourceIndex index(*g_ctx_for_plugin_filter, filename);

  if (index.tryReadCache() == false) {
    index.split();
    index.writeCache();
  }

  auto info = index.getStreamInfo();

  int numVideoFile = info->getNumVideoFile();
  if (numVideoFile != 1) {
    env->ThrowError("Amatsukzeフィルタテスト: 指定されたTSソースの映像フォーマットが複数あるためテストで使用できません");
  }

  info->prepareEncode();

	auto clip = new AMTSource(
		*g_ctx_for_plugin_filter,
		filename,
		filename + ".amaud",
    info->getFormat(0, 0),
		info->getFilterSourceFrames(0),
		info->getFilterSourceAudioFrames(0),
		env);

  // StreamReformInfoをライフタイム管理用に持たせておく
  clip->TransferStreamInfo(std::move(info));

  return clip;
}

class AVSLosslessSource : public IClip
{
	LosslessVideoFile file;
	CCodecPointer codec;
	VideoInfo vi;
	std::unique_ptr<uint8_t[]> codedFrame;
	std::unique_ptr<uint8_t[]> rawFrame;
public:
	AVSLosslessSource(AMTContext& ctx, const std::string& filepath, const VideoFormat& format, IScriptEnvironment* env)
		: file(ctx, filepath, "rb")
		, codec(make_unique_ptr(CCodec::CreateInstance(UTVF_ULH0, "Amatsukaze")))
		, vi()
	{
		file.readHeader();
		vi.width = file.getWidth();
		vi.height = file.getHeight();
		assert(format.width == vi.width);
		assert(format.height == vi.height);
		vi.num_frames = file.getNumFrames();
		vi.pixel_type = VideoInfo::CS_YV12;
		vi.SetFPS(format.frameRateNum, format.frameRateDenom);
		auto extra = file.getExtra();
		if (codec->DecodeBegin(UTVF_YV12, vi.width, vi.height, CBGROSSWIDTH_WINDOWS, extra.data(), (int)extra.size())) {
			THROW(RuntimeException, "failed to DecodeBegin (UtVideo)");
		}

		size_t codedSize = codec->EncodeGetOutputSize(UTVF_YV12, vi.width, vi.height);
		codedFrame = std::unique_ptr<uint8_t[]>(new uint8_t[codedSize]);

		rawFrame = std::unique_ptr<uint8_t[]>(new uint8_t[vi.width * vi.height * 3 / 2]);
	}

	~AVSLosslessSource() {
		codec->DecodeEnd();
	}

	PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env)
	{
		n = std::max(0, std::min(vi.num_frames - 1, n));
		file.readFrame(n, codedFrame.get());
		codec->DecodeFrame(rawFrame.get(), codedFrame.get());
		PVideoFrame dst = env->NewVideoFrame(vi);
		CopyYV12(dst, rawFrame.get(), vi.width, vi.height);
		return dst;
	}

	void __stdcall GetAudio(void* buf, __int64 start, __int64 count, IScriptEnvironment* env) { return; }
	const VideoInfo& __stdcall GetVideoInfo() { return vi; }
	bool __stdcall GetParity(int n) { return false; }
	
	int __stdcall SetCacheHints(int cachehints, int frame_range)
	{
		if (cachehints == CACHE_GET_MTMODE) return MT_SERIALIZED;
		return 0;
	};
};

} // namespace av {
