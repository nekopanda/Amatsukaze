/**
* Create encoder source with avisynth filters
* Copyright (c) 2017-2018 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include <memory>
#include <numeric>

#include "StreamUtils.hpp"
#include "ReaderWriterFFmpeg.hpp"
#include "TranscodeSetting.hpp"
#include "StreamReform.hpp"
#include "AMTSource.hpp"
#include "InterProcessComm.hpp"

class RFFExtractor
{
public:
  void clear() {
    prevFrame_ = nullptr;
  }

  void inputFrame(av::EncodeWriter& encoder, std::unique_ptr<av::Frame>&& frame, PICTURE_TYPE pic) {

    // PTSはinputFrameで再定義されるので修正しないでそのまま渡す
    switch (pic) {
    case PIC_FRAME:
    case PIC_TFF:
    case PIC_TFF_RFF:
      encoder.inputFrame(*frame);
      break;
    case PIC_FRAME_DOUBLING:
      encoder.inputFrame(*frame);
      encoder.inputFrame(*frame);
      break;
    case PIC_FRAME_TRIPLING:
      encoder.inputFrame(*frame);
      encoder.inputFrame(*frame);
      encoder.inputFrame(*frame);
      break;
    case PIC_BFF:
      encoder.inputFrame(*mixFields(
        (prevFrame_ != nullptr) ? *prevFrame_ : *frame, *frame));
      break;
    case PIC_BFF_RFF:
      encoder.inputFrame(*mixFields(
        (prevFrame_ != nullptr) ? *prevFrame_ : *frame, *frame));
      encoder.inputFrame(*frame);
      break;
    }

    prevFrame_ = std::move(frame);
  }

private:
  std::unique_ptr<av::Frame> prevFrame_;

  // 2つのフレームのトップフィールド、ボトムフィールドを合成
  static std::unique_ptr<av::Frame> mixFields(av::Frame& topframe, av::Frame& bottomframe)
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
    dst->height = top->height;

    // メモリ確保
    if (av_frame_get_buffer(dst, 64) != 0) {
      THROW(RuntimeException, "failed to allocate frame buffer");
    }

    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get((AVPixelFormat)(dst->format));
    int pixel_shift = (desc->comp[0].depth > 8) ? 1 : 0;
    int nplanes = (dst->format != AV_PIX_FMT_NV12) ? 3 : 2;

    for (int i = 0; i < nplanes; ++i) {
      int hshift = (i > 0 && dst->format != AV_PIX_FMT_NV12) ? desc->log2_chroma_w : 0;
      int vshift = (i > 0) ? desc->log2_chroma_h : 0;
      int wbytes = (dst->width >> hshift) << pixel_shift;
      int height = dst->height >> vshift;

      for (int y = 0; y < height; y += 2) {
        uint8_t* dst0 = dst->data[i] + dst->linesize[i] * (y + 0);
        uint8_t* dst1 = dst->data[i] + dst->linesize[i] * (y + 1);
        uint8_t* src0 = top->data[i] + top->linesize[i] * (y + 0);
        uint8_t* src1 = bottom->data[i] + bottom->linesize[i] * (y + 1);
        memcpy(dst0, src0, wbytes);
        memcpy(dst1, src1, wbytes);
      }
    }

    return std::move(dstframe);
  }
};

static PICTURE_TYPE getPictureTypeFromAVFrame(AVFrame* frame)
{
  bool interlaced = frame->interlaced_frame != 0;
  bool tff = frame->top_field_first != 0;
  int repeat = frame->repeat_pict;
  if (interlaced == false) {
    switch (repeat) {
    case 0: return PIC_FRAME;
    case 1: return tff ? PIC_TFF_RFF : PIC_BFF_RFF;
    case 2: return PIC_FRAME_DOUBLING;
    case 4: return PIC_FRAME_TRIPLING;
    default: THROWF(FormatException, "Unknown repeat count: %d", repeat);
    }
    return PIC_FRAME;
  }
  else {
    if (repeat) {
      THROW(FormatException, "interlaced and repeat ???");
    }
    return tff ? PIC_TFF : PIC_BFF;
  }
}

enum FILTER_PHASE {
  PHASE_PRE_PROCESS = 0,
  PHASE_GEN_TIMING = 1,
  PHASE_GEN_IMAGE = 2,
};

class AMTFilterSource : public AMTObject {
public:
  // Main (+ Post)
  AMTFilterSource(AMTContext&ctx,
    const ConfigWrapper& setting,
    const StreamReformInfo& reformInfo,
    const std::vector<EncoderZone>& zones,
    const std::string& logopath,
    int fileId, int encoderId, CMType cmtype,
    const ResourceManger& rm)
    : AMTObject(ctx)
    , setting_(setting)
    , env_(make_unique_ptr((IScriptEnvironment2*)nullptr))
    , filterTmp(setting.getAvsTmpPath(fileId, encoderId, cmtype))
  {
    try {
			// フィルタ前処理用リソース確保
			int gpuIndex = rm.wait(HOST_CMD_Filter);
			std::vector<int> outFrames;

			//Kパスまである場合
			//<=K-2: 前処理
			//K-1: タイミング生成
			//K: 画像生成（タイミングも同時に生成）
			//PHASE
			//0: 前処理
			//1: タイミング生成
			//2: 画像生成
			int pass = 0;
			for (; pass < 4; ++pass) {
				int phase = FilterPass(pass, gpuIndex, fileId, encoderId, cmtype, outFrames, reformInfo, logopath);
				if (phase == PHASE_PRE_PROCESS) {
					// 前処理を実行
					ReadAllFrames(pass, phase);
				}
				else if (phase == PHASE_GEN_TIMING) {
					// タイミング生成
					ReadAllFrames(pass, phase);
				}
				else {
					break;
				}
			}

			// エンコード用リソース確保
			int encodeIndex = rm.request(HOST_CMD_Encode);
			if (encodeIndex == -1 || encodeIndex != gpuIndex) {
				// 確保できなかった or GPUが変更されたら 一旦解放する
				filter_ = nullptr;
				env_ = nullptr;
				if (encodeIndex == -1) {
					// リソースが確保できていなかったら確保できるまで待つ
					encodeIndex = rm.wait(HOST_CMD_Encode);
				}
				// 再生成
				gpuIndex = encodeIndex;
				FilterPass(pass, gpuIndex, fileId, encoderId, cmtype, outFrames, reformInfo, logopath);
			}

      std::string postpath = setting.getPostFilterScriptPath();
      if (postpath.size()) {
        env_->SetVar("AMT_SOURCE", filter_);
        filter_ = env_->Invoke("Import", postpath.c_str(), 0).AsClip();
      }

      if (setting_.isDumpFilter()) {
          StringBuilder sb;
          sb.append("DumpFilterGraph(\"%s\", 1)", setting_.getFilterGraphDumpPath());
          env_->SetVar("last", filter_);
          env_->Invoke("Eval", AVSValue(sb.str().c_str()));
      }

      MakeZones(filter_, fileId, encoderId, outFrames, zones, reformInfo);

      MakeOutFormat(reformInfo.getFormat(encoderId, fileId).videoFormat);
    }
    catch (const AvisynthError& avserror) {
      // AvisynthErrorはScriptEnvironmentに依存しているので
      // AviSynthExceptionに変換する
      THROWF(AviSynthException, "%s", avserror.msg);
    }
  }

  ~AMTFilterSource() {
    filter_ = nullptr;
    env_ = nullptr;
  }

  const PClip& getClip() const {
    return filter_;
  }

  const VideoFormat& getFormat() const {
    return outfmt_;
  }

  // 入力ゾーンのtrim後のゾーンを返す
  const std::vector<EncoderZone> getZones() const {
    return outZones_;
  }

  // 各フレームのFPS（FrameTimingがない場合サイズゼロ）
  const std::vector<int>& getFrameDurations() const {
    return frameDurations;
  }

  IScriptEnvironment2* getEnv() const {
    return env_.get();
  }

private:
  const ConfigWrapper& setting_;
  ScriptEnvironmentPointer env_;
  PClip filter_;
  VideoFormat outfmt_;
  std::vector<EncoderZone> outZones_;
  std::vector<int> frameDurations;
  std::string filterTmp;

  std::string makePreamble() {
    StringBuilder sb;
    if (setting_.isDumpFilter()) {
        sb.append("SetGraphAnalysis(true)\n");
    }
    // システムのプラグインフォルダを無効化
    if (setting_.isSystemAvsPlugin() == false) {
      sb.append("ClearAutoloadDirs()\n");
    }
    // Amatsukaze用オートロードフォルダを追加
    sb.append("AddAutoloadDir(\"%s\\plugins64\")\n", GetModuleDirectory());
    // メモリ節約オプションを有効にする
    sb.append("SetCacheMode(CACHE_OPTIMAL_SIZE)\n");
    return sb.str();
  }

  void InitEnv() {
    env_ = nullptr;
    env_ = make_unique_ptr(CreateScriptEnvironment2());
    env_->Invoke("Eval", AVSValue(makePreamble().c_str()));

    AVSValue avsv;
    env_->LoadPlugin(GetModulePath().c_str(), true, &avsv);
    env_->SetVar("AMT_TMP", env_->SaveString(filterTmp.c_str()));
  }

  void ReadAllFrames(int pass, int phase) {
    const VideoInfo vi = filter_->GetVideoInfo();

    ctx.info("フィルタパス%d 予定フレーム数: %d", pass + 1, vi.num_frames);
    Stopwatch sw;
    sw.start();
    int prevFrames = 0;

    if (phase == PHASE_GEN_TIMING) {
      frameDurations.clear();
    }

    for (int i = 0; i < vi.num_frames; ) {
      PVideoFrame frame = filter_->GetFrame(i, env_.get());
      if (phase == PHASE_GEN_TIMING) {
        frameDurations.push_back(std::max(1, frame->GetProperty("FrameDuration", 1)));
        i += frameDurations.back();
      }
      else {
        ++i;
      }
      double elapsed = sw.current();
      if (elapsed >= 1.0) {
        double fps = (i - prevFrames) / elapsed;
        ctx.progress("%dフレーム完了 %.2ffps", i, fps);

        prevFrames = i;
        sw.stop();
      }
    }

    ctx.info("フィルタパス%d 完了: %.2f秒", pass + 1, sw.getTotal());
  }

  PClip prefetch(PClip clip, int threads) {
    AVSValue args[] = { clip, threads };
    return env_->Invoke("Prefetch", AVSValue(args, 2)).AsClip();
  }

  PClip makeMainFilterSource(
    int fileId, int encoderId, CMType cmtype,
    std::vector<int>& outFrames,
    const StreamReformInfo& reformInfo,
    const std::string& logopath)
  {
    auto& fmt = reformInfo.getFormat(encoderId, fileId);
    PClip clip = new av::AMTSource(ctx,
      setting_.getIntVideoFilePath(fileId),
      setting_.getWaveFilePath(),
      fmt.videoFormat, fmt.audioFormat[0],
      reformInfo.getFilterSourceFrames(fileId),
      reformInfo.getFilterSourceAudioFrames(fileId),
      setting_.getDecoderSetting(),
      env_.get());

    clip = prefetch(clip, 1);

    if (setting_.isNoDelogo() == false && logopath.size() > 0) {
      // キャッシュを間に入れるためにInvokeでフィルタをインスタンス化
      AVSValue args_a[] = { clip, logopath.c_str() };
      PClip analyzeclip = env_->Invoke("AMTAnalyzeLogo", AVSValue(args_a, 2)).AsClip();
      AVSValue args_e[] = { clip, analyzeclip, logopath.c_str() };
      clip = env_->Invoke("AMTEraseLogo2", AVSValue(args_e, 3)).AsClip();
      clip = prefetch(clip, 1);
    }

    return trimInput(clip, fileId, encoderId, cmtype, outFrames, reformInfo);
  }

  PClip trimInput(PClip clip, int fileId, int encoderId, CMType cmtype,
    std::vector<int>& outFrames,
    const StreamReformInfo& reformInfo)
  {
    // このencoderIndex+cmtype用の出力フレームリスト作成
    auto& srcFrames = reformInfo.getFilterSourceFrames(fileId);
    outFrames.clear();
    for (int i = 0; i < (int)srcFrames.size(); ++i) {
      int frameEncoderIndex = reformInfo.getEncoderIndex(srcFrames[i].frameIndex);
      if (encoderId == frameEncoderIndex) {
        if (cmtype == CMTYPE_BOTH || cmtype == srcFrames[i].cmType) {
          outFrames.push_back(i);
        }
      }
    }
    int numSrcFrames = (int)outFrames.size();

    // 不連続点で区切る
    std::vector<EncoderZone> trimZones;
    EncoderZone zone;
    zone.startFrame = outFrames.front();
    for (int i = 1; i < (int)outFrames.size(); ++i) {
      if (outFrames[i] != outFrames[i - 1] + 1) {
        zone.endFrame = outFrames[i - 1];
        trimZones.push_back(zone);
        zone.startFrame = outFrames[i];
      }
    }
    zone.endFrame = outFrames.back();
    trimZones.push_back(zone);

    if (trimZones.size() > 1 ||
      trimZones[0].startFrame != 0 ||
      trimZones[0].endFrame != (srcFrames.size() - 1))
    {
      // Trimが必要
      std::vector<AVSValue> trimClips(trimZones.size());
      for (int i = 0; i < (int)trimZones.size(); ++i) {
        AVSValue arg[] = { clip, trimZones[i].startFrame, trimZones[i].endFrame };
        trimClips[i] = env_->Invoke("Trim", AVSValue(arg, 3));
      }
      if (trimClips.size() == 1) {
        clip = trimClips[0].AsClip();
      }
      else {
        clip = env_->Invoke("AlignedSplice", AVSValue(trimClips.data(), (int)trimClips.size())).AsClip();
      }
    }

    return clip;
  }

	int FilterPass(int pass, int gpuIndex,
		int fileId, int encoderId, CMType cmtype,
		std::vector<int>& outFrames,
		const StreamReformInfo& reformInfo,
		const std::string& logopath)
	{
		outFrames.clear();

		filter_ = nullptr; // クリップ解放
		InitEnv();
		filter_ = makeMainFilterSource(fileId, encoderId, cmtype, outFrames, reformInfo, logopath);
		env_->SetVar("AMT_SOURCE", filter_);
		env_->SetVar("AMT_PASS", pass);
		env_->SetVar("AMT_DEV", gpuIndex);

		std::string mainpath = setting_.getFilterScriptPath();
		if (mainpath.size()) {
			filter_ = env_->Invoke("Import", mainpath.c_str(), 0).AsClip();
			return env_->GetVarDef("AMT_PHASE", PHASE_GEN_IMAGE).AsInt();
		}
		return PHASE_GEN_IMAGE;
	}

  void MakeZones(
    PClip postClip,
    int fileId, int encoderId,
    const std::vector<int>& outFrames,
    const std::vector<EncoderZone>& zones,
    const StreamReformInfo& reformInfo)
  {
    int numSrcFrames = (int)outFrames.size();

    // このencoderIndex用のゾーンを作成
    outZones_.clear();
    for (int i = 0; i < (int)zones.size(); ++i) {
      EncoderZone newZone = {
        (int)(std::lower_bound(outFrames.begin(), outFrames.end(), zones[i].startFrame) - outFrames.begin()),
        (int)(std::lower_bound(outFrames.begin(), outFrames.end(), zones[i].endFrame) - outFrames.begin())
      };
      // 短すぎる場合はゾーンを捨てる
      if (newZone.endFrame - newZone.startFrame > 30) {
        outZones_.push_back(newZone);
      }
    }

    const VideoFormat& infmt = reformInfo.getFormat(encoderId, fileId).videoFormat;
    VideoInfo outvi = postClip->GetVideoInfo();
    double srcDuration = (double)numSrcFrames * infmt.frameRateDenom / infmt.frameRateNum;
    double clipDuration = (double)outvi.num_frames * outvi.fps_denominator / outvi.fps_numerator;
    bool outParity = postClip->GetParity(0);

    ctx.info("フィルタ入力: %dフレーム %d/%dfps (%s)",
      numSrcFrames, infmt.frameRateNum, infmt.frameRateDenom,
      infmt.progressive ? "プログレッシブ" : "インターレース");

    ctx.info("フィルタ出力: %dフレーム %d/%dfps (%s)",
      outvi.num_frames, outvi.fps_numerator, outvi.fps_denominator,
      outParity ? "インターレース" : "プログレッシブ");

    if (std::abs(srcDuration - clipDuration) > 0.1f) {
      THROWF(RuntimeException, "フィルタ出力映像の時間が入力と一致しません（入力: %.3f秒 出力: %.3f秒）", srcDuration, clipDuration);
    }

    if (numSrcFrames != outvi.num_frames && outParity) {
      ctx.warn("フレーム数が変わっていますがインターレースのままです。プログレッシブ出力が目的ならAssumeBFF()をavsファイルの最後に追加してください。");
    }

    // フレーム数が変わっている場合はゾーンを引き伸ばす
    if (numSrcFrames != outvi.num_frames) {
      double scale = (double)outvi.num_frames / numSrcFrames;
      for (int i = 0; i < (int)outZones_.size(); ++i) {
        outZones_[i].startFrame = std::max(0, std::min(outvi.num_frames, (int)std::round(outZones_[i].startFrame * scale)));
        outZones_[i].endFrame = std::max(0, std::min(outvi.num_frames, (int)std::round(outZones_[i].endFrame * scale)));
      }
    }
  }

  void MakeOutFormat(const VideoFormat& infmt)
  {
    auto vi = filter_->GetVideoInfo();
    // vi_からエンコーダ入力用VideoFormatを生成する
    outfmt_ = infmt;
    if (outfmt_.width != vi.width || outfmt_.height != vi.height) {
      // リサイズされた
      outfmt_.width = vi.width;
      outfmt_.height = vi.height;
      // リサイズされた場合はアスペクト比を1:1にする
      outfmt_.sarHeight = outfmt_.sarWidth = 1;
    }
    outfmt_.frameRateDenom = vi.fps_denominator;
    outfmt_.frameRateNum = vi.fps_numerator;
    // インターレースかどうかは取得できないのでパリティがfalse(BFF?)だったらプログレッシブと仮定
    outfmt_.progressive = (filter_->GetParity(0) == false);
  }
};

// 各フレームのFPS情報から、各種データを生成
class FilterVFRProc : public AMTObject
{
  bool is120fps;  // 120fpsタイミング
  int fpsNum, fpsDenom; // 60fpsのフレームレート
  double totalDuration; // 合計時間（チェック用）
  std::vector<int> frameFps_; // 各フレームのFPS(120fpsの場合)/各フレームの60fpsフレーム数(60fpsの場合)
  std::vector<int> frameMap_; // VFRフレーム番号 -> 60fpsフレーム番号のマッピング
  
  bool is23(const std::vector<int>& durations, int offset) {
    if (offset + 1 < durations.size()) {
      return durations[offset] == 2 && durations[offset + 1] == 3;
    }
    return false;
  }

  bool is32(const std::vector<int>& durations, int offset) {
    if (offset + 1 < durations.size()) {
      return durations[offset] == 3 && durations[offset + 1] == 2;
    }
    return false;
  }

  bool is2224(const std::vector<int>& durations, int offset) {
    if (offset + 3 < durations.size()) {
      return durations[offset] == 2 &&
        durations[offset + 1] == 2 &&
        durations[offset + 2] == 2 &&
        durations[offset + 3] == 4;
    }
    return false;
  }

public:
  FilterVFRProc(AMTContext&ctx, const std::vector<int>& durations, const VideoInfo& vi, bool is120fps)
  : AMTObject(ctx) 
  , is120fps(is120fps)
  {
    fpsNum = vi.fps_numerator;
    fpsDenom = vi.fps_denominator;

    auto it = std::find_if_not(
      durations.begin(), durations.end(), [](int n) { return n != 1; });
    if (it == durations.end()) {
      // すべて1の場合はCFR
      return;
    }

    int n = 0;
    for (int i = 0; i < (int)durations.size(); ++i) {
      frameMap_.push_back(n);
      n += durations[i];
    }

    if (is120fps) {
      // パターンが出現したところをVFR化
      // （時間がズレないようにする）
      for (int i = 0; i < (int)durations.size(); ) {
        if (is23(durations, i)) {
          frameFps_.push_back(-1);
          frameFps_.push_back(-1);
          i += 2;
        }
        else if (is32(durations, i)) {
          frameFps_.push_back(-1);
          frameFps_.push_back(-1);
          i += 2;
        }
        else if (is2224(durations, i)) {
          for (int i = 0; i < 4; ++i) {
            frameFps_.push_back(-1);
          }
          i += 4;
        }
        else {
          frameFps_.push_back(durations[i]);
          i += 1;
        }
      }
    }
    else {
      // 60fpsタイミング
      frameFps_ = durations;
    }

    assert(frameFps_.size() == frameMap_.size());

    int numFrames60 = std::accumulate(durations.begin(), durations.end(), 0);
    totalDuration = (double)numFrames60 * fpsDenom / fpsNum;
  }

  bool isEnabled() const {
    return frameMap_.size() > 0;
  }

  const std::vector<int>& getFrameMap() const {
    return frameMap_;
  }

  void toVFRZones(std::vector<EncoderZone>& zones) const {
    for (int i = 0; i < (int)zones.size(); ++i) {
      zones[i].startFrame = (int)(std::lower_bound(frameMap_.begin(), frameMap_.end(), zones[i].startFrame) - frameMap_.begin());
      zones[i].endFrame = (int)(std::lower_bound(frameMap_.begin(), frameMap_.end(), zones[i].endFrame) - frameMap_.begin());
    }
  }

  void makeTimecode(const std::string& filepath) const {
    StringBuilder sb;
    sb.append("# timecode format v2\n");
    ctx.info("[VFR] %d fpsタイミングでタイムコードを生成します", is120fps ? 120 : 60);
    if (is120fps) {
      const double timestep = (double)fpsDenom / fpsNum;
      const double time24 = (fpsDenom * 10.0) / (fpsNum * 4.0);
      double curTime = 0;
      double maxDiff = 0; // チェック用
      for (int i = 0; i < (int)frameFps_.size(); ++i) {
        maxDiff = std::max(maxDiff, std::abs(curTime - frameMap_[i] * timestep));
        sb.append("%d\n", (int)std::round(curTime * 1000));
        curTime += (frameFps_[i] == -1) ? time24 : (frameFps_[i] * timestep);
      }
      ctx.info("60fpsフレーム表示時刻とVFRタイムコードによる表示時刻との最大差: %f ms", maxDiff * 1000);
      if (std::abs(curTime - totalDuration) >= 0.000001) {
        // 1us以上のズレがあったらエラーとする
        THROWF(RuntimeException, "タイムコードの合計時間と映像時間の合計にズレがあります。(%f != %f)", curTime, totalDuration);
      }
    }
    else {
      for (int i = 0; i < (int)frameMap_.size(); ++i) {
        sb.append("%d\n", (int)std::round(frameMap_[i] * (double)fpsDenom / fpsNum * 1000));
      }
    }
    File file(filepath, "w");
    file.write(sb.getMC());
  }
};

// VFRでだいたいのレートコントロールを実現する
// VFRタイミングとCMゾーンからゾーンとビットレートを作成
std::vector<BitrateZone> MakeVFRBitrateZones(const std::vector<int>& durations,
    const std::vector<EncoderZone>& cmzones, double bitrateCM, 
    int fpsNum, int fpsDenom, double timeFactor, double costLimit)
{
  enum {
    UNIT_FRAMES = 8,
    HARD_ZONE_LIMIT = 1000, // ゾーン数上限は1000
    TARGET_ZONES_PER_HOUR = 30 // 目標ゾーン数は1時間あたり30個
  };
  struct Block {
    int index;   // ブロック先頭のUNITアドレス
    int next;    // 後ろのブロックの先頭ブロックアドレス（このブロックが存在しない場合は-1）
    double avg;  // このブロックの平均ビットレート
    double cost; // 後ろのブロックと結合したときの追加コスト
  };

  if (durations.size() == 0) {
    return std::vector<BitrateZone>();
  }
  // 8フレームごとの平均ビットレートを計算
  std::vector<double> units(nblocks((int)durations.size(), UNIT_FRAMES));
  for (int i = 0; i < (int)units.size(); ++i) {
    auto start = durations.begin() + i * UNIT_FRAMES;
    auto end = ((i + 1) * UNIT_FRAMES < durations.size()) ? start + UNIT_FRAMES : durations.end();
    int sum = std::accumulate(start, end, 0);
		double invfps = (double)sum / (int)(end - start);
    units[i] = (invfps - 1.0) * timeFactor + 1.0;
  }
  // cmzonesを適用
  for (int i = 0; i < (int)cmzones.size(); ++i) {
    // 半端部分はCMゾーンを小さくる方向に丸める
    int start = nblocks(cmzones[i].startFrame, UNIT_FRAMES);
    int end = cmzones[i].endFrame / UNIT_FRAMES;
    for (int k = start; k < end; ++k) {
      units[k] *= bitrateCM;
    }
  }
  // ここでのunitsは各フレームに適用すべきビットレート
  // だが、そのままzonesにすると数が多すぎて
  // コマンドライン引数にできないのである程度まとめる
  std::vector<Block> blocks;
  double cur = units[0];
  blocks.push_back(Block{ 0, 1, cur, 0 });
  // 同じビットレートの連続はまとめる
  for (int i = 1; i < (int)units.size(); ++i) {
    if (units[i] != cur) {
      cur = units[i];
      blocks.push_back(Block{ i, (int)blocks.size() + 1, cur, 0 });
    }
  }
  // 最後に番兵を置く
  blocks.push_back(Block{ (int)units.size(), -1, 0, 0 });

  auto sumDiff = [&](int start, int end, double avg) {
    double diff = 0;
    for (int i = start; i < end; ++i) {
      diff += std::abs(units[i] - avg);
    }
    return diff;
  };

  auto calcCost = [&](Block& cur, const Block&  next) {
    int start = cur.index;
    int mid = next.index;
    int end = blocks[next.next].index;
    // 現在のコスト
    
    double cur_cost = sumDiff(start, mid, cur.avg);
    double next_cost = sumDiff(mid, end, next.avg);
    // 連結後の平均ビットレート
    double avg2 = (cur.avg * (mid - start) + next.avg * (end - mid)) / (end - start);
    // 連結後のコスト
    double cost2 = sumDiff(start, end, avg2);
    // 追加コスト
    cur.cost = cost2 - (cur_cost + next_cost);
  };

  // 連結時追加コスト計算
  for (int i = 0; blocks[i].index < (int)units.size(); i = blocks[i].next) {
    auto& cur = blocks[i];
    auto& next = blocks[cur.next];
    // 次のブロックが存在すれば
    if (next.index < (int)units.size()) {
      calcCost(cur, next);
    }
  }

  // 最大ブロック数
  int totalDuration = std::accumulate(durations.begin(), durations.end(), 0);
  auto totalHours = totalDuration * (double)fpsDenom / fpsNum / 3600.0;
  int targetNumZones = std::max(1, (int)(TARGET_ZONES_PER_HOUR * totalHours));
  double totalCostLimit = units.size() * costLimit;

  // ヒープ作成
  auto comp = [&](int b0, int b1) {
    return blocks[b0].cost > blocks[b1].cost;
  };
  // 最後のブロックと番兵は連結できないので除く
  int heapSize = (int)blocks.size() - 2;
  int numZones = heapSize;
  std::vector<int> indices(heapSize);
  for (int i = 0; i < heapSize; ++i) indices[i] = i;
  std::make_heap(indices.begin(), indices.begin() + heapSize, comp);
  double totalCost = 0;
  while ((totalCost < totalCostLimit && numZones > targetNumZones) ||
    numZones > HARD_ZONE_LIMIT)
  {
    // 追加コスト最小ブロック
    int idx = indices.front();
    std::pop_heap(indices.begin(), indices.begin() + (heapSize--), comp);
    auto& cur = blocks[idx];
    // このブロックが既に連結済みでなければ
    if (cur.next != -1) {
      auto& next = blocks[cur.next];
      int start = cur.index;
      int mid = next.index;
      int end = blocks[next.next].index;
      totalCost += cur.cost;
      // 連結後の平均ビットレートに更新
      cur.avg = (cur.avg * (mid - start) + next.avg * (end - mid)) / (end - start);
      // 連結後のnextに更新
      cur.next = next.next;
      // 連結されるブロックは無効化
      next.next = -1;
      --numZones;
      // 更に次のブロックがあれば
      auto& nextnext = blocks[cur.next];
      if (nextnext.index < (int)units.size()) {
        // 連結時の追加コストを計算
        calcCost(cur, nextnext);
        // 再度ヒープに追加
        indices[heapSize] = idx;
        std::push_heap(indices.begin(), indices.begin() + (++heapSize), comp);
      }
    }
  }

  // 結果を生成
  std::vector<BitrateZone> zones;
  for (int i = 0; blocks[i].index < (int)units.size(); i = blocks[i].next) {
    const auto& cur = blocks[i];
    BitrateZone zone = BitrateZone();
    zone.startFrame = cur.index * UNIT_FRAMES;
    zone.endFrame = std::min((int)durations.size(), blocks[cur.next].index * UNIT_FRAMES);
    zone.bitrate = cur.avg;
    zones.push_back(zone);
  }

  return zones;
}

// VFRに対応していないエンコーダでビットレート指定を行うとき用の
// 平均フレームレートを考慮したビットレートを計算する
double AdjustVFRBitrate(const std::vector<int>& durations)
{
  if (durations.size() == 0) {
    return 1.0;
  }
  int totalDurations = std::accumulate(durations.begin(), durations.end(), 0);
  return (double)totalDurations / durations.size();
}
