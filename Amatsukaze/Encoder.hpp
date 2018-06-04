/**
* Output frames to encoder
* Copyright (c) 2017-2018 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include "ReaderWriterFFmpeg.hpp"
#include "TranscodeSetting.hpp"
#include "FilteredSource.hpp"

class AMTFilterVideoEncoder : public AMTObject {
public:
  AMTFilterVideoEncoder(
    AMTContext&ctx)
    : AMTObject(ctx)
    , thread_(this, 16)
  { }

  void encode(
    PClip source, VideoFormat outfmt, 
    const std::vector<std::string>& encoderOptions,
    IScriptEnvironment* env)
  {
    vi_ = source->GetVideoInfo();
    outfmt_ = outfmt;

    frameDurations.clear();

    int bufsize = outfmt_.width * outfmt_.height * 3;

    int npass = (int)encoderOptions.size();
    for (int i = 0; i < npass; ++i) {
      ctx.info("%d/%dパス エンコード開始 予定フレーム数: %d", i + 1, npass, vi_.num_frames);

      const std::string& args = encoderOptions[i];

      // 初期化
      encoder_ = std::unique_ptr<av::EncodeWriter>(new av::EncodeWriter(ctx));

      ctx.info("[エンコーダ起動]");
      ctx.info(args.c_str());
      encoder_->start(args, outfmt_, false, bufsize);

      Stopwatch sw;
      // エンコードスレッド開始
      thread_.start();
      sw.start();

      bool error = false;

      try {
        // エンコード
        bool is_first_pass = (i == 0);
        for (int f = 0, i = 0; f < vi_.num_frames; ) {
          auto frame = source->GetFrame(f, env);
          thread_.put(std::unique_ptr<PVideoFrame>(new PVideoFrame(frame)), 1);
          if (is_first_pass) {
            frameDurations.push_back(std::max(1, frame->GetProperty("FrameDuration", 1)));
            f += frameDurations.back();
          }
          else {
            f += frameDurations[i++];
          }
        }
      }
      catch (Exception&) {
        error = true;
      }

      // エンコードスレッドを終了して自分に引き継ぐ
      thread_.join();

      // 残ったフレームを処理
      encoder_->finish();

      if (error) {
        THROW(RuntimeException, "エンコード中に不明なエラーが発生");
      }

      encoder_ = nullptr;
      sw.stop();

      double prod, cons; thread_.getTotalWait(prod, cons);
      ctx.info("Total: %.2fs, FilterWait: %.2fs, EncoderWait: %.2fs", sw.getTotal(), prod, cons);
    }
  }

  std::vector<int> getFrameDurations() const {
    return frameDurations;
  }

private:

  class SpDataPumpThread : public DataPumpThread<std::unique_ptr<PVideoFrame>, true> {
  public:
    SpDataPumpThread(AMTFilterVideoEncoder* this_, int bufferingFrames)
      : DataPumpThread(bufferingFrames)
      , this_(this_)
    { }
  protected:
    virtual void OnDataReceived(std::unique_ptr<PVideoFrame>&& data) {
      this_->onFrameReceived(std::move(data));
    }
  private:
    AMTFilterVideoEncoder * this_;
  };

  VideoInfo vi_;
  VideoFormat outfmt_;
  std::unique_ptr<av::EncodeWriter> encoder_;
  std::vector<int> frameDurations;

  SpDataPumpThread thread_;

  int getFFformat() {
    switch (vi_.BitsPerComponent()) {
    case 8: return AV_PIX_FMT_YUV420P;
    case 10: return AV_PIX_FMT_YUV420P10;
    case 12: return AV_PIX_FMT_YUV420P12;
    case 14: return AV_PIX_FMT_YUV420P14;
    case 16: return AV_PIX_FMT_YUV420P16;
    default: THROW(FormatException, "サポートされていないフィルタ出力形式です");
    }
    return 0;
  }

  void onFrameReceived(std::unique_ptr<PVideoFrame>&& frame) {

    // PVideoFrameをav::Frameに変換
    const PVideoFrame& in = *frame;
    av::Frame out;

    out()->width = outfmt_.width;
    out()->height = outfmt_.height;
    out()->format = getFFformat();
    out()->sample_aspect_ratio.num = outfmt_.sarWidth;
    out()->sample_aspect_ratio.den = outfmt_.sarHeight;
    out()->color_primaries = (AVColorPrimaries)outfmt_.colorPrimaries;
    out()->color_trc = (AVColorTransferCharacteristic)outfmt_.transferCharacteristics;
    out()->colorspace = (AVColorSpace)outfmt_.colorSpace;

    // AVFrame.dataは16バイトまで超えたアクセスがあり得るので、
    // そのままポインタをセットすることはできない

    if (av_frame_get_buffer(out(), 64) != 0) {
      THROW(RuntimeException, "failed to allocate frame buffer");
    }

    const uint8_t *src_data[4] = {
      in->GetReadPtr(PLANAR_Y),
      in->GetReadPtr(PLANAR_U),
      in->GetReadPtr(PLANAR_V),
      nullptr
    };
    int src_linesize[4] = {
      in->GetPitch(PLANAR_Y),
      in->GetPitch(PLANAR_U),
      in->GetPitch(PLANAR_V),
      0
    };

    av_image_copy(
      out()->data, out()->linesize, src_data, src_linesize,
      (AVPixelFormat)out()->format, out()->width, out()->height);

    encoder_->inputFrame(out);
  }

};

class AMTSimpleVideoEncoder : public AMTObject {
public:
  AMTSimpleVideoEncoder(
    AMTContext& ctx,
    const ConfigWrapper& setting)
    : AMTObject(ctx)
    , setting_(setting)
    , reader_(this)
    , thread_(this, 8)
  {
    //
  }

  void encode()
  {
    if (setting_.isTwoPass()) {
      ctx.info("1/2パス エンコード開始");
      processAllData(1);
      ctx.info("2/2パス エンコード開始");
      processAllData(2);
    }
    else {
      processAllData(-1);
    }
  }

  int getAudioCount() const {
    return audioCount_;
  }

  int64_t getSrcFileSize() const {
    return srcFileSize_;
  }

  VideoFormat getVideoFormat() const {
    return videoFormat_;
  }

private:
  class SpVideoReader : public av::VideoReader {
  public:
    SpVideoReader(AMTSimpleVideoEncoder* this_)
      : VideoReader(this_->ctx)
      , this_(this_)
    { }
  protected:
    virtual void onFileOpen(AVFormatContext *fmt) {
      this_->onFileOpen(fmt);
    }
    virtual void onVideoFormat(AVStream *stream, VideoFormat fmt) {
      this_->onVideoFormat(stream, fmt);
    }
    virtual void onFrameDecoded(av::Frame& frame) {
      this_->onFrameDecoded(frame);
    }
    virtual void onAudioPacket(AVPacket& packet) {
      this_->onAudioPacket(packet);
    }
  private:
    AMTSimpleVideoEncoder * this_;
  };

  class SpDataPumpThread : public DataPumpThread<std::unique_ptr<av::Frame>> {
  public:
    SpDataPumpThread(AMTSimpleVideoEncoder* this_, int bufferingFrames)
      : DataPumpThread(bufferingFrames)
      , this_(this_)
    { }
  protected:
    virtual void OnDataReceived(std::unique_ptr<av::Frame>&& data) {
      this_->onFrameReceived(std::move(data));
    }
  private:
    AMTSimpleVideoEncoder * this_;
  };

  class AudioFileWriter : public av::AudioWriter {
  public:
    AudioFileWriter(AVStream* stream, const std::string& filename, int bufsize)
      : AudioWriter(stream, bufsize)
      , file_(filename, "wb")
    { }
  protected:
    virtual void onWrite(MemoryChunk mc) {
      file_.write(mc);
    }
  private:
    File file_;
  };

  const ConfigWrapper& setting_;
  SpVideoReader reader_;
  av::EncodeWriter* encoder_;
  SpDataPumpThread thread_;

  int audioCount_;
  std::vector<std::unique_ptr<AudioFileWriter>> audioFiles_;
  std::vector<int> audioMap_;

  int64_t srcFileSize_;
  VideoFormat videoFormat_;
  RFFExtractor rffExtractor_;

  int pass_;

  void onFileOpen(AVFormatContext *fmt)
  {
    audioMap_ = std::vector<int>(fmt->nb_streams, -1);
    if (pass_ <= 1) { // 2パス目は出力しない
      audioCount_ = 0;
      for (int i = 0; i < (int)fmt->nb_streams; ++i) {
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
          audioFiles_.emplace_back(new AudioFileWriter(
            fmt->streams[i], setting_.getIntAudioFilePath(0, 0, audioCount_, CMTYPE_BOTH), 8 * 1024));
          audioMap_[i] = audioCount_++;
        }
      }
    }
  }

  void processAllData(int pass)
  {
    pass_ = pass;

    encoder_ = new av::EncodeWriter(ctx);

    // エンコードスレッド開始
    thread_.start();

    // エンコード
    reader_.readAll(setting_.getSrcFilePath(), setting_.getDecoderSetting());

    // エンコードスレッドを終了して自分に引き継ぐ
    thread_.join();

    // 残ったフレームを処理
    encoder_->finish();

    if (pass_ <= 1) { // 2パス目は出力しない
      for (int i = 0; i < audioCount_; ++i) {
        audioFiles_[i]->flush();
      }
      audioFiles_.clear();
    }

    rffExtractor_.clear();
    audioMap_.clear();
    delete encoder_; encoder_ = NULL;
  }

  void onVideoFormat(AVStream *stream, VideoFormat fmt)
  {
    videoFormat_ = fmt;

    // ビットレート計算
    File file(setting_.getSrcFilePath(), "rb");
    srcFileSize_ = file.size();
    double srcBitrate = ((double)srcFileSize_ * 8 / 1000) / (stream->duration * av_q2d(stream->time_base));
    ctx.info("入力映像ビットレート: %d kbps", (int)srcBitrate);

    if (setting_.isAutoBitrate()) {
      ctx.info("目標映像ビットレート: %d kbps",
        (int)setting_.getBitrate().getTargetBitrate(fmt.format, srcBitrate));
    }

    // 初期化
    std::string args = makeEncoderArgs(
      setting_.getEncoder(),
      setting_.getEncoderPath(),
      setting_.getOptions(
        fmt.format, srcBitrate, false, pass_, std::vector<BitrateZone>(), 0, 0, CMTYPE_BOTH),
      fmt,
      setting_.getEncVideoFilePath(0, 0, CMTYPE_BOTH));

    ctx.info("[エンコーダ開始]");
    ctx.info(args.c_str());

    // x265でインタレースの場合はフィールドモード
    bool dstFieldMode =
      (setting_.getEncoder() == ENCODER_X265 && fmt.progressive == false);

    int bufsize = fmt.width * fmt.height * 3;
    encoder_->start(args, fmt, dstFieldMode, bufsize);
  }

  void onFrameDecoded(av::Frame& frame__) {
    // フレームをコピーしてスレッドに渡す
    thread_.put(std::unique_ptr<av::Frame>(new av::Frame(frame__)), 1);
  }

  void onFrameReceived(std::unique_ptr<av::Frame>&& frame)
  {
    // RFFフラグ処理
    // PTSはinputFrameで再定義されるので修正しないでそのまま渡す
    PICTURE_TYPE pic = getPictureTypeFromAVFrame((*frame)());
    //fprintf(stderr, "%s\n", PictureTypeString(pic));
    rffExtractor_.inputFrame(*encoder_, std::move(frame), pic);

    //encoder_.inputFrame(*frame);
  }

  void onAudioPacket(AVPacket& packet)
  {
    if (pass_ <= 1) { // 2パス目は出力しない
      int audioIdx = audioMap_[packet.stream_index];
      if (audioIdx >= 0) {
        audioFiles_[audioIdx]->inputFrame(packet);
      }
    }
  }
};
