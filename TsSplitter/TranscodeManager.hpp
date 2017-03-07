#pragma once

#include <string>
#include <sstream>
#include <memory>

#include "StreamUtils.hpp"
#include "TsSplitter.hpp"
#include "Transcode.hpp"
#include "StreamReform.hpp"
#include "PacketCache.hpp"

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

enum ENUM_ENCODER {
  ENCODER_X264,
  ENCODER_X265,
  ENCODER_QSVENC,
};

static std::string makeEncoderArgs(
  ENUM_ENCODER encoder,
  const std::string& binpath,
  const std::string& options,
  const VideoFormat& fmt,
  const std::string& outpath)
{
  std::ostringstream ss;

  ss << "\"" << binpath << "\"";

  // y4mヘッダにあるので必要ない
  //ss << " --fps " << fmt.frameRateNum << "/" << fmt.frameRateDenom;
  //ss << " --input-res " << fmt.width << "x" << fmt.height;
  //ss << " --sar " << fmt.sarWidth << ":" << fmt.sarHeight;

  ss << " --colorprim " << av::getColorPrimStr(fmt.colorPrimaries);
  ss << " --transfer " << av::getTransferCharacteristicsStr(fmt.transferCharacteristics);
  ss << " --colormatrix " << av::getColorSpaceStr(fmt.colorSpace);

  // インターレース
  switch (encoder) {
  case ENCODER_X264:
  case ENCODER_QSVENC:
    ss << (fmt.progressive ? "" : " --tff");
    break;
  case ENCODER_X265:
    ss << (fmt.progressive ? " --no-interlace" : " --interlace tff");
    break;
  }

  ss << " " << options << " -o \"" << outpath << "\"";

  // 入力形式
  switch (encoder) {
  case ENCODER_X264:
    ss << " --demuxer y4m -";
    break;
  case ENCODER_X265:
    ss << " --y4m --input -";
    break;
  case ENCODER_QSVENC:
    ss << " --y4m -i -";
    break;
  }
  
  return ss.str();
}

static std::string makeMuxerArgs(
  const std::string& binpath,
  const std::string& inVideo,
  const std::vector<std::string>& inAudios,
  const std::string& outpath)
{
  std::ostringstream ss;

  ss << "\"" << binpath << "\"";
  ss << " -i \"" << inVideo << "\"";
  for (const auto& inAudio : inAudios) {
    ss << " -i \"" << inAudio << "\"";
  }
  ss << " \"" << outpath << "\"";

  return ss.str();
}

struct TranscoderSetting {
  // 入力ファイルパス（拡張子を含む）
  std::string tsFilePath;
  // 出力ファイルパス（拡張子を除く）
  std::string outVideoPath;
  // 中間ファイルプリフィックス
  std::string intFileBasePath;
  // 一時音声ファイルパス（拡張子を含む）
  std::string audioFilePath;
  // エンコーダ設定
  ENUM_ENCODER encoder;
  std::string encoderPath;
  std::string encoderOptions;
  std::string muxerPath;

  std::string getIntVideoFilePath(int index) const
  {
    std::ostringstream ss;
    ss << intFileBasePath << "-" << index << ".mpg";
    return ss.str();
  }

  std::string getEncVideoFilePath(int vindex, int index) const
  {
    std::ostringstream ss;
    ss << intFileBasePath << "-" << vindex << "-" << index << ".raw";
    return ss.str();
  }

  std::string getIntAudioFilePath(int vindex, int index, int aindex) const
  {
    std::ostringstream ss;
    ss << intFileBasePath << "-" << vindex << "-" << index << "-" << aindex << ".aac";
    return ss.str();
  }

  std::string getOutFilePath(int index) const
  {
    if (index == 0) {
      return outVideoPath + ".mp4";
    }
    std::string ret = outVideoPath + "-";
    ret += index;
    ret += ".mp4";
    return ret;
  }
};


class AMTSplitter : public TsSplitter {
public:
  AMTSplitter(AMTContext& ctx, const TranscoderSetting& setting)
    : TsSplitter(ctx)
    , setting_(setting)
    , psWriter(ctx)
    , writeHandler(*this)
    , audioFile_(setting.audioFilePath, "wb")
    , videoFileCount_(0)
    , videoStreamType_(-1)
    , audioStreamType_(-1)
    , audioFileSize_(0)
  {
    psWriter.setHandler(&writeHandler);
  }

  StreamReformInfo split() {
    readAll();

    // for debug
    printInteraceCount();

    return StreamReformInfo(ctx, videoFileCount_,
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

  const TranscoderSetting& setting_;
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

  void readAll() {
    enum { BUFSIZE = 4 * 1024 * 1024 };
    auto buffer_ptr = std::unique_ptr<uint8_t[]>(new uint8_t[BUFSIZE]);
    MemoryChunk buffer(buffer_ptr.get(), BUFSIZE);
    File srcfile(setting_.tsFilePath, "rb");
    size_t readBytes;
    do {
      readBytes = srcfile.read(buffer);
      inputTsData(MemoryChunk(buffer.data, readBytes));
    } while (readBytes == buffer.length);
  }

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
    ctx.info("時間: %f 秒", totalTime / 90000.0);

    ctx.info("フレームカウンタ");
    ctx.info("FRAME=%d DBL=%d TLP=%d TFF=%d BFF=%d TFF_RFF=%d BFF_RFF=%d",
      interaceCounter[0], interaceCounter[1], interaceCounter[2], interaceCounter[3], interaceCounter[4], interaceCounter[5], interaceCounter[6]);

    for (const auto& pair : PTSdiffMap) {
      ctx.info("(PTS_Diff,Cnt)=(%d,%d)\n", pair.first, pair.second.v);
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
    ctx.debug("映像フォーマット変更を検知");
    ctx.debug("サイズ: %dx%d FPS: %d/%d", fmt.width, fmt.height, fmt.frameRateNum, fmt.frameRateDenom);

    // 出力ファイルを変更
    writeHandler.open(setting_.getIntVideoFilePath(videoFileCount_++));
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
		ctx.debug("音声 %d のフォーマット変更を検知", audioIdx);
    ctx.debug("チャンネル: %s サンプルレート: %d",
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

class AMTVideoEncoder : public AMTObject {
public:
  AMTVideoEncoder(
    AMTContext&ctx,
    const TranscoderSetting& setting,
    const StreamReformInfo& reformInfo)
    : AMTObject(ctx)
    , setting_(setting)
    , reformInfo_(reformInfo)
  {
    //
  }

  ~AMTVideoEncoder() {
    delete[] encoders_; encoders_ = NULL;
  }

  void encode(int videoFileIndex) {
    videoFileIndex_ = videoFileIndex;

    int numEncoders = reformInfo_.getNumEncoders(videoFileIndex);
    if (numEncoders == 0) {
      ctx.warn("numEncoders == 0 ...");
      return;
    }

    const auto& format0 = reformInfo_.getFormat(0, videoFileIndex);
    int bufsize = format0.videoFormat.width * format0.videoFormat.height * 3;

    // 初期化
    encoders_ = new av::EncodeWriter[numEncoders_];
    SpVideoReader reader(this);

    for (int i = 0; i < numEncoders_; ++i) {
      const auto& format = reformInfo_.getFormat(i, videoFileIndex);
      std::string arg = makeEncoderArgs(
        setting_.encoder,
        setting_.encoderPath,
        setting_.encoderOptions,
        format.videoFormat,
        setting_.getEncVideoFilePath(videoFileIndex, i));
      encoders_[i].start(arg, format.videoFormat, bufsize);
    }

    // エンコード
    std::string intVideoFilePath = setting_.getIntVideoFilePath(videoFileIndex);
    reader.readAll(intVideoFilePath);

    // 終了処理
    for (int i = 0; i < numEncoders_; ++i) {
      encoders_[i].finish();
    }

    delete[] encoders_; encoders_ = NULL;
    numEncoders_ = 0;

    // 中間ファイル削除
    remove(intVideoFilePath.c_str());
  }

private:
  class SpVideoReader : public av::VideoReader {
  public:
    SpVideoReader(AMTVideoEncoder* this_)
      : VideoReader()
      , this_(this_)
    { }
  protected:
    virtual void onFrameDecoded(av::Frame& frame) {
      this_->onFrameDecoded(frame);
    }
  private:
    AMTVideoEncoder* this_;
  };

  const TranscoderSetting& setting_;
  const StreamReformInfo& reformInfo_;

  int videoFileIndex_;
  int numEncoders_;
  av::EncodeWriter* encoders_;

  void onFrameDecoded(av::Frame& frame__) {

    // TODO: thread
    // TODO: process pic_type
    // copy reference
    av::Frame frame = frame__;

    int64_t pts = frame()->pts;
    int frameIndex = reformInfo_.getVideoFrameIndex(pts, videoFileIndex_);
    if (frameIndex == -1) {
      THROWF(FormatException, "Unknown PTS frame %lld", pts);
    }

    int encoderIndex = reformInfo_.getEncoderIndex(frameIndex);
    
    encoders_[encoderIndex].inputFrame(frame);
  }
};

class AMTMuxder : public AMTObject {
public:
  AMTMuxder(
    AMTContext&ctx,
    const TranscoderSetting& setting,
    const StreamReformInfo& reformInfo)
    : AMTObject(ctx)
    , setting_(setting)
    , reformInfo_(reformInfo)
    , audioCache_(ctx, setting.audioFilePath, reformInfo.getAudioFileOffsets(), 12, 4)
  { }

  void mux(int videoFileIndex) {
    int numEncoders = reformInfo_.getNumEncoders(videoFileIndex);
    if (numEncoders == 0) {
      return;
    }

    for (int i = 0; i < numEncoders; ++i) {
      // 音声ファイルを作成
      std::vector<std::string> audioFiles;
      const FileAudioFrameList& fileFrameList =
        reformInfo_.getFileAudioFrameList(i, videoFileIndex);
      for (int a = 0; a < (int)fileFrameList.size(); ++a) {
        const std::vector<int>& frameList = fileFrameList[a];
        if (frameList.size() > 0) {
          std::string filepath = setting_.getIntAudioFilePath(videoFileIndex, i, a);
          File file(filepath, "wb");
          for (int frameIndex : frameList) {
            file.write(audioCache_[frameIndex]);
          }
          audioFiles.push_back(filepath);
        }
      }

      // Mux
      std::string encVideoFile = setting_.getEncVideoFilePath(videoFileIndex, i);
      std::string outFilePath = setting_.getOutFilePath(
        reformInfo_.getOutFileIndex(i, videoFileIndex));
      std::string args = makeMuxerArgs(
        setting_.muxerPath, encVideoFile, audioFiles, outFilePath);

      {
        MySubProcess muxer(args);
      }

      // 中間ファイル削除
      for (const std::string& audioFile : audioFiles) {
        remove(audioFile.c_str());
      }
      remove(encVideoFile.c_str());
    }
  }

private:
  class MySubProcess : public EventBaseSubProcess {
  public:
    MySubProcess(const std::string& args) : EventBaseSubProcess(args) { }
  protected:
    virtual void onOut(bool isErr, MemoryChunk mc) {
      // これはマルチスレッドで呼ばれるの注意
      fwrite(mc.data, mc.length, 1, isErr ? stderr : stdout);
    }
  };

  const TranscoderSetting& setting_;
  const StreamReformInfo& reformInfo_;

  PacketCache audioCache_;
};

static void transcodeMain(AMTContext& ctx, const TranscoderSetting& setting)
{
  auto splitter = std::unique_ptr<AMTSplitter>(new AMTSplitter(ctx, setting));
  StreamReformInfo reformInfo = splitter->split();
  splitter = nullptr;

  reformInfo.prepareEncode();

  auto encoder = std::unique_ptr<AMTVideoEncoder>(new AMTVideoEncoder(ctx, setting, reformInfo));
  for (int i = 0; i < reformInfo.getNumVideoFile(); ++i) {
    encoder->encode(i);
  }
  encoder = nullptr;

  reformInfo.prepareMux();

  auto muxer = std::unique_ptr<AMTMuxder>(new AMTMuxder(ctx, setting, reformInfo));
  for (int i = 0; i < reformInfo.getNumVideoFile(); ++i) {
    muxer->mux(i);
  }
  muxer = nullptr;
}

