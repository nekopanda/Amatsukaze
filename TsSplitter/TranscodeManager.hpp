#pragma once

#include <string>
#include <sstream>
#include <memory>

#include "StreamUtils.hpp"
#include "TsSplitter.hpp"

// カラースペース定義を使うため
#include "libavutil/pixfmt.h"

namespace av {

// カラースペース3セット
// x265は数値そのままでもOKだが、x264はhelpを見る限りstringでなければ
// ならないようなので変換を定義
// とりあえずARIB STD-B32 v3.7に書いてあるのだけ

// 3原色
static const char* getColorPrimStr(int color_prim) {
  switch (color_prim) {
  case AVCOL_PRI_BT709: return "bt709";
  case AVCOL_PRI_BT2020: return "bt2020";
  default:
    THROWF(FormatException,
      "Unsupported color primaries (%d)", color_prim);
  }
  return NULL;
}

// ガンマ
static const char* getTransferCharacteristicsStr(int transfer_characteritics) {
  switch (transfer_characteritics) {
  case AVCOL_TRC_BT709: return "bt709";
  case AVCOL_TRC_IEC61966_2_4: return "iec61966-2-4";
  case AVCOL_TRC_BT2020_10: return "bt2020-10";
  case AVCOL_TRC_SMPTEST2084: return "smpte-st-2084";
  case AVCOL_TRC_ARIB_STD_B67: return "arib-std-b67";
  default:
    THROWF(FormatException,
      "Unsupported color transfer characteritics (%d)", transfer_characteritics);
  }
  return NULL;
}

// 変換係数
static const char* getColorSpaceStr(int color_space) {
  switch (color_space) {
  case AVCOL_SPC_BT709: return "bt709";
  case AVCOL_SPC_BT2020_NCL: return "bt2020nc";
  default:
    THROWF(FormatException,
      "Unsupported color color space (%d)", color_space);
  }
  return NULL;
}

} // namespace av {

static std::string makeArgs(
  const std::string& binpath,
  const std::string& options,
  const VideoFormat& fmt,
  const std::string& outpath)
{
  std::ostringstream ss;

  ss << "\"" << binpath << "\"";
  ss << " --demuxer y4m";

  // y4mヘッダにあるので必要ない
  //ss << " --fps " << fmt.frameRateNum << "/" << fmt.frameRateDenom;
  //ss << " --input-res " << fmt.width << "x" << fmt.height;
  //ss << " --sar " << fmt.sarWidth << ":" << fmt.sarHeight;

  ss << " --colorprim " << av::getColorPrimStr(fmt.colorPrimaries);
  ss << " --transfer " << av::getTransferCharacteristicsStr(fmt.transferCharacteristics);
  ss << " --colormatrix " << av::getColorSpaceStr(fmt.colorSpace);

  if (fmt.progressive == false) {
    ss << " --tff";
  }

  ss << " " << options << " -o \"" << outpath << "\" -";
  
  return ss.str();
}

static std::string makeIntVideoFilePath(const std::string& basepath, int index)
{
  std::ostringstream ss;
  ss << basepath << "-" << index << ".mpg";
  return ss.str();
}

struct FirstPhaseSetting {
  std::string videoBasePath;
  std::string audioFilePath;
};

struct FileAudioFrameInfo : public AudioFrameInfo {
  int audioIdx;
  int codedDataSize;
  int64_t fileOffset;

  FileAudioFrameInfo()
    : AudioFrameInfo()
    , audioIdx(0)
    , codedDataSize(0)
    , fileOffset(0)
  { }

  FileAudioFrameInfo(const AudioFrameInfo& info)
    : AudioFrameInfo(info)
    , audioIdx(0)
    , codedDataSize(0)
    , fileOffset(0)
  { }
};

enum StreamEventType {
  STREAM_EVENT_NONE = 0,
  PID_TABLE_CHANGED,
  VIDEO_FORMAT_CHANGED,
  AUDIO_FORMAT_CHANGED
};

struct StreamEvent {
  StreamEventType type;
  int frameIdx;  // フレーム番号
  int audioIdx;  // 変更された音声インデックス（AUDIO_FORMAT_CHANGEDのときのみ有効）
  int numAudio;  // 音声の数（PID_TABLE_CHANGEDのときのみ有効）
};

typedef std::vector<std::unique_ptr<std::vector<int>>> FileAudioFrameList;

struct OutVideoFormat {
  int formatId; // 内部フォーマットID（通し番号）
  int videoFileId;
  VideoFormat videoFormat;
  std::vector<AudioFormat> audioFormat;
};

class StreamReformInfo {
public:
  StreamReformInfo(
    int numVideoFile,
    std::vector<VideoFrameInfo>& videoFrameList,
    std::vector<FileAudioFrameInfo>& audioFrameList,
    std::vector<StreamEvent>& streamEventList)
    : numVideoFile_(numVideoFile)
    , videoFrameList_(std::move(videoFrameList))
    , audioFrameList_(std::move(audioFrameList))
    , streamEventList_(std::move(streamEventList))
  {
    // TODO:
    encodedFrames_.resize(videoFrameList_.size(), false);
  }

  // PTS -> video frame index
  int getVideoFrameIndex(int64_t PTS, int videoFileIndex) const {
    auto it = framePtsMap_.find(PTS);
    if (it == framePtsMap_.end()) {
      return -1;
    }
    
    // TODO: check videoFileIndex

    return it->second;
  }

  int getNumEncoders(int videoFileIndex) const {
    return int(
      outFormatStartIndex_[videoFileIndex + 1] - outFormatStartIndex_[videoFileIndex]);
  }

  // video frame index -> VideoFrameInfo
  const VideoFrameInfo& getVideoFrameInfo(int frameIndex) const {
    return videoFrameList_[frameIndex];
  }

  // video frame index -> encoder index
  int getEncoderIndex(int frameIndex) const {
    int formatId = frameFormatId_[frameIndex];
    const auto& format = outFormat_[formatId];
    return formatId - outFormatStartIndex_[format.videoFileId];
  }

  // フレームをエンコードしたフラグをセット
  void frameEncoded(int frameIndex) {
    encodedFrames_[frameIndex] = true;
  }

  const OutVideoFormat& getFormat(int encoderIndex, int videoFileIndex) const {
    int formatId = outFormatStartIndex_[videoFileIndex] + encoderIndex;
    return outFormat_[formatId];
  }

  void prepare3rdPhase() {
    // TODO:
  }

  const std::unique_ptr<FileAudioFrameList>& getFileAudioFrameList(
    int encoderIndex, int videoFileIndex)
  {
    int formatId = outFormatStartIndex_[videoFileIndex] + encoderIndex;
    return reformedAudioFrameList_[formatId];
  }

private:
  // 1st phase 出力
  int numVideoFile_;
  std::vector<VideoFrameInfo> videoFrameList_;
  std::vector<FileAudioFrameInfo> audioFrameList_;
  std::vector<StreamEvent> streamEventList_;

  // 計算データ
  std::vector<int64_t> modifiedPTS_; // ラップアラウンドしないPTS
  std::vector<int64_t> dataPTS_; // 映像フレームのストリーム上での位置とPTSの関連付け
  std::vector<int64_t> streamEventPTS_;

  std::vector<OutVideoFormat> outFormat_;
  // 中間映像ファイルごとのフォーマット開始インデックス
  // サイズは中間映像ファイル数+1
  std::vector<int> outFormatStartIndex_;

  // 2nd phase 入力
  std::vector<int> frameFormatId_; // videoFrameList_と同じサイズ
  std::map<int64_t, int> framePtsMap_;

  // 2nd phase 出力
  std::vector<bool> encodedFrames_;

  // 3rd phase 入力
  std::vector<std::unique_ptr<FileAudioFrameList>> reformedAudioFrameList_;

  void reformMain()
  {
    if (videoFrameList_.size() == 0) {
      THROW(FormatException, "映像フレームが1枚もありません");
    }
    if (audioFrameList_.size() == 0) {
      THROW(FormatException, "音声フレームが1枚もありません");
    }
    if (streamEventList_.size() == 0 || streamEventList_[0].type != PID_TABLE_CHANGED) {
      THROW(FormatException, "不正なデータです");
    }

    // framePtsMap_を作成（すぐに作れるので）
    for (int i = 0; i < int(videoFrameList_.size()); ++i) {
      framePtsMap_[videoFrameList_[i].PTS] = i;
    }

    // ラップアラウンドしないPTSを生成
    modifiedPTS_.reserve(videoFrameList_.size());
    int64_t prevPTS = videoFrameList_[0].PTS;
    for (int i = 0; i < int(videoFrameList_.size()); ++i) {
      int64_t PTS = videoFrameList_[i].PTS;
      int64_t modPTS = prevPTS + int64_t((int32_t(PTS) - int32_t(prevPTS)));
      modifiedPTS_.push_back(modPTS);
      prevPTS = PTS;
    }

    // ストリームが戻っている場合は処理できないのでエラーとする
    for (int i = 1; i < int(videoFrameList_.size()); ++i) {
      if (modifiedPTS_[i] - modifiedPTS_[i - 1] < -60 * MPEG_CLOCK_HZ) {
        // 1分以上戻っていたらエラーとする
        THROWF(FormatException,
          "PTSが戻っています。処理できません。 %llu -> %llu",
          modifiedPTS_[i - 1], modifiedPTS_[i]);
      }
    }

    // dataPTSを生成
    // 後ろから見てその時点で最も小さいPTSをdataPTSとする
    int64_t curMin = INT64_MAX;
    int64_t curMax = 0;
    dataPTS_.resize(videoFrameList_.size());
    for (int i = (int)videoFrameList_.size() - 1; i >= 0; --i) {
      curMin = std::min(curMin, modifiedPTS_[i]);
      curMax = std::max(curMax, modifiedPTS_[i]);
      dataPTS_[i] = curMin;
    }

    // ストリームイベントのPTSを計算
    int64_t exceedLastPTS = curMax + 1;
    streamEventPTS_.resize(streamEventList_.size());
    for (int i = 0; i < (int)streamEventList_.size(); ++i) {
      auto& ev = streamEventList_[i];
      int64_t pts = -1;
      if (ev.type == PID_TABLE_CHANGED || ev.type == VIDEO_FORMAT_CHANGED) {
        if (ev.frameIdx >= (int)videoFrameList_.size()) {
          // 後ろ過ぎて対象のフレームがない
          pts = exceedLastPTS;
        }
        else {
          pts = dataPTS_[ev.frameIdx];
        }
      }
      else if (ev.type == AUDIO_FORMAT_CHANGED) {
        if (ev.frameIdx >= (int)audioFrameList_.size()) {
          // 後ろ過ぎて対象のフレームがない
          pts = exceedLastPTS;
        }
        else {
          pts = audioFrameList_[ev.frameIdx].PTS;
        }
      }
      streamEventPTS_[i] = pts;
    }

    struct SingleFormatSection {
      int formatId;
      int64_t fromPTS, toPTS;
    };

    // 時間的に近いストリームイベントを1つの変化点とみなす
    const int64_t CHANGE_TORELANCE = 3 * MPEG_CLOCK_HZ;

    std::vector<SingleFormatSection> sectionList;

    OutVideoFormat curFormat = OutVideoFormat();
    SingleFormatSection curSection = SingleFormatSection();
    int64_t curFromPTS = -1;
    curFormat.videoFileId = -1;
    for (int i = 0; i < (int)streamEventList_.size(); ++i) {
      auto& ev = streamEventList_[i];
      int64_t pts = streamEventPTS_[i];
      if (pts >= exceedLastPTS) {
        // 後ろに映像がなければ意味がない
        continue;
      }
      if (curFromPTS == -1) { // 最初
        curFromPTS = curSection.fromPTS = pts;
      }
      else if (curFromPTS + CHANGE_TORELANCE < pts) {
        // 区間を追加
        curSection.toPTS = pts;
        registerOrGetFormat(curFormat);
        curSection.formatId = curFormat.formatId;
        sectionList.push_back(curSection);

        curFromPTS = curSection.fromPTS = pts;
      }
      // 変更を反映
      switch (ev.type) {
      case PID_TABLE_CHANGED:
        curFormat.audioFormat.resize(ev.numAudio);
        break;
      case VIDEO_FORMAT_CHANGED:
        // ファイル変更
        ++curFormat.videoFileId;
        outFormatStartIndex_.push_back(outFormat_.size());
        curFormat.videoFormat = videoFrameList_[ev.frameIdx].format;
        break;
      case AUDIO_FORMAT_CHANGED:
        if (curFormat.audioFormat.size() >= ev.audioIdx) {
          THROW(FormatException, "StreamEvent's audioIdx exceeds numAudio of the previous table change event");
        }
        curFormat.audioFormat[ev.audioIdx] = audioFrameList_[ev.audioIdx].format;
        break;
      }
    }
    // 最後の区間を追加
    curSection.toPTS = exceedLastPTS;
    registerOrGetFormat(curFormat);
    curSection.formatId = curFormat.formatId;
    sectionList.push_back(curSection);
    outFormatStartIndex_.push_back(outFormat_.size());

    // frameFormatId_を生成
    frameFormatId_.resize(videoFrameList_.size());
    for (int i = 0; i < int(videoFrameList_.size()); ++i) {
      int64_t pts = modifiedPTS_[i];
      // 区間を探す
      int formatId = std::partition_point(sectionList.begin(), sectionList.end(),
        [=](const SingleFormatSection& sec) {
          return !(pts < sec.toPTS);
        })->formatId;
      frameFormatId_[i] = formatId;
    }
  }

  void registerOrGetFormat(OutVideoFormat& format) {
    // すでにあるのから探す
    for (int i = outFormatStartIndex_.back(); i < (int)outFormat_.size(); ++i) {
      if (isEquealFormat(outFormat_[i], format)) {
        format.formatId = i;
        return;
      }
    }
    // ないので登録
    format.formatId = (int)outFormat_.size();
    outFormat_.push_back(format);
  }

  bool isEquealFormat(const OutVideoFormat& a, const OutVideoFormat& b) {
    if (a.videoFormat != b.videoFormat) return false;
    if (a.audioFormat.size() != b.audioFormat.size()) return false;
    for (int i = 0; i < (int)a.audioFormat.size(); ++i) {
      if (a.audioFormat[i] != b.audioFormat[i]) {
        return false;
      }
    }
    return true;
  }

  int getEncoderIdx(const OutVideoFormat& format) {
    return format.formatId - outFormatStartIndex_[format.videoFileId];
  }

  static int numGenerateFrames(const VideoFrameInfo& frameInfo) {
    // BFF RFFだけ2枚、他は1枚
    return (frameInfo.pic == PIC_BFF_RFF) ? 2 : 1;
  }

  void genAudioStream() {
    // TODO: encodedFrames_から音声ストリームのフレーム列を生成
  }
};

class FirstPhaseConverter : public TsSplitter {
public:
  FirstPhaseConverter(TsSplitterContext *ctx, FirstPhaseSetting* setting)
    : TsSplitter(ctx)
    , setting_(setting)
    , psWriter(ctx)
    , writeHandler(*this)
    , audioFile_(setting->audioFilePath, "wb")
    , videoFileCount_(0)
    , videoStreamType_(-1)
    , audioStreamType_(-1)
    , audioFileSize_(0)
  {
    psWriter.setHandler(&writeHandler);
  }

  StreamReformInfo reformInfo() {
    
    // for debug
    printInteraceCount();

    return StreamReformInfo(videoFileCount_,
      videoFrameList_, audioFrameList_, streamEventList_);
  }

protected:
  class StreamFileWriteHandler : public PsStreamWriter::EventHandler {
    TsSplitter& this_;
    std::unique_ptr<File> file_;
  public:
    StreamFileWriteHandler(TsSplitter& this_)
      : this_(this_) { }
    virtual void onStreamData(MemoryChunk mc) {
      if (file_ != NULL) {
        file_->write(mc);
      }
    }
    void open(const std::string& path) {
      file_ = std::unique_ptr<File>(new File(path, "wb"));
    }
    void close() {
      file_ = nullptr;
    }
  };

  FirstPhaseSetting* setting_;
  PsStreamWriter psWriter;
  StreamFileWriteHandler writeHandler;
  File audioFile_;

  int videoFileCount_;
  int videoStreamType_;
  int audioStreamType_;
  int64_t audioFileSize_;

  // データ
  std::vector<VideoFrameInfo> videoFrameList_;
  std::vector<FileAudioFrameInfo> audioFrameList_;
  std::vector<StreamEvent> streamEventList_;


  static bool CheckPullDown(PICTURE_TYPE p0, PICTURE_TYPE p1) {
    switch (p0) {
    case PIC_TFF:
    case PIC_BFF_RFF:
      return (p1 == PIC_TFF || p1 == PIC_TFF_RFF);
    case PIC_BFF:
    case PIC_TFF_RFF:
      return (p1 == PIC_BFF || p1 == PIC_BFF_RFF);
    default: // それ以外はチェック対象外
      return true;
    }
  }

  void printInteraceCount() {

    if (videoFrameList_.size() == 0) {
      printf("フレームがありません");
      return;
    }

    // ラップアラウンドしないPTSを生成
    std::vector<std::pair<int64_t, int>> modifiedPTS;
    int64_t videoBasePTS = videoFrameList_[0].PTS;
    int64_t prevPTS = videoFrameList_[0].PTS;
    for (int i = 0; i < int(videoFrameList_.size()); ++i) {
      int64_t PTS = videoFrameList_[i].PTS;
      int64_t modPTS = prevPTS + int64_t((int32_t(PTS) - int32_t(prevPTS)));
      modifiedPTS.push_back(std::make_pair(modPTS, i));
      prevPTS = PTS;
    }

    // PTSでソート
    std::sort(modifiedPTS.begin(), modifiedPTS.end());

    // フレームリストを出力
    FILE* framesfp = fopen("frames.txt", "w");
    fprintf(framesfp, "FrameNumber,DecodeFrameNumber,PTS,Duration,FRAME_TYPE,PIC_TYPE,IsGOPStart\n");
    for (int i = 0; i < (int)modifiedPTS.size(); ++i) {
      int64_t PTS = modifiedPTS[i].first;
      int decodeIndex = modifiedPTS[i].second;
      const VideoFrameInfo& frame = videoFrameList_[decodeIndex];
      int PTSdiff = -1;
      if (i < (int)modifiedPTS.size() - 1) {
        int64_t nextPTS = modifiedPTS[i + 1].first;
        const VideoFrameInfo& nextFrame = videoFrameList_[modifiedPTS[i + 1].second];
        PTSdiff = int(nextPTS - PTS);
        if (CheckPullDown(frame.pic, nextFrame.pic) == false) {
          printf("Flag Check Error: PTS=%lld %s -> %s\n",
            PTS, PictureTypeString(frame.pic), PictureTypeString(nextFrame.pic));
        }
      }
      fprintf(framesfp, "%d,%d,%lld,%d,%s,%s,%d\n",
        i, decodeIndex, PTS, PTSdiff, FrameTypeString(frame.type), PictureTypeString(frame.pic), frame.isGopStart ? 1 : 0);
    }
    fclose(framesfp);

    // PTS間隔を出力
    struct Integer {
      int v;
      Integer() : v(0) { }
    };

    std::array<int, MAX_PIC_TYPE> interaceCounter = { 0 };
    std::map<int, Integer> PTSdiffMap;
    prevPTS = -1;
    for (const auto& ptsIndex : modifiedPTS) {
      int64_t PTS = ptsIndex.first;
      const VideoFrameInfo& frame = videoFrameList_[ptsIndex.second];
      interaceCounter[(int)frame.pic]++;
      if (prevPTS != -1) {
        int PTSdiff = int(PTS - prevPTS);
        PTSdiffMap[PTSdiff].v++;
      }
      prevPTS = PTS;
    }

    int64_t totalTime = modifiedPTS.back().first - videoBasePTS;
    ctx->info("時間: %f 秒", totalTime / 90000.0);

    ctx->info("フレームカウンタ");
    ctx->info("FRAME=%d DBL=%d TLP=%d TFF=%d BFF=%d TFF_RFF=%d BFF_RFF=%d",
      interaceCounter[0], interaceCounter[1], interaceCounter[2], interaceCounter[3], interaceCounter[4], interaceCounter[5], interaceCounter[6]);

    for (const auto& pair : PTSdiffMap) {
      ctx->info("(PTS_Diff,Cnt)=(%d,%d)\n", pair.first, pair.second.v);
    }
  }

  // TsSplitter仮想関数 //

  virtual void onVideoPesPacket(
    int64_t clock,
    const std::vector<VideoFrameInfo>& frames,
    PESPacket packet)
  {
		for (const VideoFrameInfo& frame : frames) {
      videoFrameList_.push_back(frame);
    }
    psWriter.outVideoPesPacket(clock, frames, packet);
  }

  virtual void onVideoFormatChanged(VideoFormat fmt) {
    ctx->debug("映像フォーマット変更を検知");
    ctx->debug("サイズ: %dx%d FPS: %d/%d", fmt.width, fmt.height, fmt.frameRateNum, fmt.frameRateDenom);

    // 出力ファイルを変更
    writeHandler.open(makeIntVideoFilePath(setting_->videoBasePath, videoFileCount_++));
    psWriter.outHeader(videoStreamType_, audioStreamType_);

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
    MemoryChunk payload = packet.paylod();
    audioFile_.write(payload);

    int64_t offset = 0;
    for (const AudioFrameData& frame : frames) {
      FileAudioFrameInfo info = frame;
      info.audioIdx = audioIdx;
      info.codedDataSize = frame.codedDataSize;
      info.fileOffset = audioFileSize_ + offset;
      offset += frame.codedDataSize;
      audioFrameList_.push_back(info);
    }

    ASSERT(offset == payload.length);
    audioFileSize_ += payload.length;

    if (videoFileCount_ > 0) {
      psWriter.outAudioPesPacket(audioIdx, clock, frames, packet);
    }
  }

  virtual void onAudioFormatChanged(int audioIdx, AudioFormat fmt) {
		ctx->debug("音声 %d のフォーマット変更を検知", audioIdx);
    ctx->debug("チャンネル: %s サンプルレート: %d",
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
    videoStreamType_ = video.stype;
    audioStreamType_ = audio[0].stype;

    StreamEvent ev = StreamEvent();
    ev.type = PID_TABLE_CHANGED;
    ev.numAudio = (int)audio.size();
    ev.frameIdx = (int)videoFrameList_.size();
    streamEventList_.push_back(ev);
  }
};
