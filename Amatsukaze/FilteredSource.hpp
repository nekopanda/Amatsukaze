/**
* Create encoder source with avisynth filters
* Copyright (c) 2017-2018 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include <memory>

#include "StreamUtils.hpp"
#include "TranscodeSetting.hpp"
#include "StreamReform.hpp"
#include "AMTSource.hpp"
#include "AMTFrameTiming.hpp"

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

class AMTFilterSource : public AMTObject {
public:
  // Main (+ Post)
  AMTFilterSource(AMTContext&ctx,
    const ConfigWrapper& setting,
    const StreamReformInfo& reformInfo,
    const std::vector<EncoderZone>& zones,
    const std::string& logopath,
    int fileId, int encoderId, CMType cmtype)
    : AMTObject(ctx)
    , setting_(setting)
    , env_(make_unique_ptr((IScriptEnvironment2*)nullptr))
  {
    try {
      InitEnv();
      std::vector<int> outFrames;
      filter_ = makeMainFilterSource(fileId, encoderId, cmtype, outFrames, reformInfo, logopath);

      std::string mainpath = setting.getFilterScriptPath();
      if (mainpath.size()) {
        env_->SetVar("AMT_SOURCE", filter_);
        env_->SetVar("AMT_GEN_TIME", true);
        filter_ = env_->Invoke("Import", mainpath.c_str(), 0).AsClip();

        const AMTFrameTiming* frameTiming = AMTFrameTiming::GetParam(filter_->GetVideoInfo());
        if (frameTiming) { // is time code clip
                           // TODO: gen time code
                           //

                           // AMT_GEN_TIME=falseで作り直す
          InitEnv();
          std::vector<int> outFrames_;
          filter_ = makeMainFilterSource(fileId, encoderId, cmtype, outFrames_, reformInfo, logopath);

          env_->SetVar("AMT_SOURCE", filter_);
          env_->SetVar("AMT_GEN_TIME", false);
          filter_ = env_->Invoke("Import", mainpath.c_str(), 0).AsClip();
        }
      }

      std::string postpath = setting.getPostFilterScriptPath();
      if (postpath.size()) {
        env_->SetVar("AMT_SOURCE", filter_);
        filter_ = env_->Invoke("Import", postpath.c_str(), 0).AsClip();
      }

      MakeZones(filter_, fileId, encoderId, outFrames, zones, reformInfo);

      MakeOutFormat(reformInfo.getFormat(encoderId, fileId).videoFormat);
    }
    catch (const AvisynthError& avserror) {
      // AvisynthErrorはScriptEnvironmentに依存しているので
      // AviSyntExceptioに変換する
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
  const std::vector<int>& getFrameFps() const {
    return frameFps;
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
  std::vector<int> frameFps;

  std::string makePreamble() {
    StringBuilder sb;
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
    env_ = make_unique_ptr(CreateScriptEnvironment2());
    env_->Invoke("Eval", AVSValue(makePreamble().c_str()));

    AVSValue avsv;
    env_->LoadPlugin(GetModulePath().c_str(), true, &avsv);
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
class FilterVFRProc
{
  int fpsNum, fpsDenom; // 60fpsのフレームレート
  std::vector<int> frameFps_; // 各フレームのFPS
  std::vector<int> frameMap_; // VFRフレーム番号 -> 60fpsフレーム番号のマッピング

  bool is30(const std::vector<AMTFrameFps>& frameFps, int offset) {
    auto it = frameFps.begin() + offset;
    if (offset + 2 <= (int)frameFps.size()) {
      AMTFrameFps f0 = it[0];
      AMTFrameFps f1 = it[1];
      return f0.fps == AMT_FPS_30 && f1.fps == AMT_FPS_30 && f0.source == f1.source;
    }
    return false;
  }

  bool is23(const std::vector<AMTFrameFps>& frameFps, int offset) {
    auto it = frameFps.begin() + offset;
    if (offset + 5 <= (int)frameFps.size()) {
      for (int i = 0; i < 5; ++i) {
        if (it[i].fps != AMT_FPS_24) return false;
      }
      return it[0].source == it[1].source && it[2].source == it[3].source && it[3].source == it[4].source;
    }
    return false;
  }

  bool is32(const std::vector<AMTFrameFps>& frameFps, int offset) {
    auto it = frameFps.begin() + offset;
    if (offset + 5 <= (int)frameFps.size()) {
      for (int i = 0; i < 5; ++i) {
        if (it[i].fps != AMT_FPS_24) return false;
      }
      return it[0].source == it[1].source && it[1].source == it[2].source && it[3].source == it[4].source;
    }
    return false;
  }

  bool is2224(const std::vector<AMTFrameFps>& frameFps, int offset) {
    auto it = frameFps.begin() + offset;
    if (offset + 10 <= (int)frameFps.size()) {
      for (int i = 0; i < 10; ++i) {
        if (it[i].fps != AMT_FPS_24) return false;
      }
      return it[0].source == it[1].source &&
        it[2].source == it[3].source &&
        it[4].source == it[5].source &&
        it[6].source == it[7].source &&
        it[7].source == it[8].source &&
        it[8].source == it[9].source;
    }
    return false;
  }

public:
  FilterVFRProc(const std::vector<AMTFrameFps>& frameFps, const VideoInfo& vi) {
    fpsNum = vi.fps_numerator;
    fpsDenom = vi.fps_denominator;

    // パターンが出現したところをVFR化
    // （時間がズレないようにする）
    for (int i = 0; i < (int)frameFps.size(); ) {
      frameMap_.push_back(i);
      if (is30(frameFps, i)) {
        frameFps_.push_back(AMT_FPS_30);
        i += 2;
      }
      else if (is23(frameFps, i)) {
        frameFps_.push_back(AMT_FPS_24);
        frameFps_.push_back(AMT_FPS_24);
        frameMap_.push_back(i + 2);
        i += 5;
      }
      else if (is32(frameFps, i)) {
        frameFps_.push_back(AMT_FPS_24);
        frameFps_.push_back(AMT_FPS_24);
        frameMap_.push_back(i + 3);
        i += 5;
      }
      else if (is2224(frameFps, i)) {
        for (int i = 0; i < 4; ++i) {
          frameFps_.push_back(AMT_FPS_24);
        }
        frameMap_.push_back(i + 2);
        frameMap_.push_back(i + 4);
        frameMap_.push_back(i + 6);
        i += 10;
      }
      else {
        frameFps_.push_back(AMT_FPS_60);
        i += 1;
      }
    }
  }

  const std::vector<int>& getFrameMap() {
    return frameMap_;
  }

  void toVFRZones(std::vector<EncoderZone>& zones) {
    for (int i = 0; i < (int)zones.size(); ++i) {
      zones[i].startFrame = (int)(std::lower_bound(frameMap_.begin(), frameMap_.end(), zones[i].startFrame) - frameMap_.begin());
      zones[i].endFrame = (int)(std::lower_bound(frameMap_.begin(), frameMap_.end(), zones[i].endFrame) - frameMap_.begin());
    }
  }

  void makeTimecode(const std::string& filepath) {
    const double durations[4] = {
      0,
      (fpsNum * 10.0) / (fpsDenom * 4.0), // AMT_FPS_24
      (fpsNum * 2.0) / fpsDenom, // AMT_FPS_30
      (double)fpsNum / fpsDenom // AMT_FPS_60
    };
    StringBuilder sb;
    sb.append("# timecode format v2\n");
    double curTime = 0;
    for (int i = 0; i < (int)frameFps_.size(); ++i) {
      sb.append("%d\n", (int)std::round(curTime * 1000));
      curTime += durations[frameFps_[i]];
    }
    File file(filepath, "w");
    file.write(sb.getMC());
  }
};
