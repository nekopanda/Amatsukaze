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

class Y4MWriter {
	static const char* getPixelFormat(VideoInfo vi) {
		if (vi.Is420()) {
			switch (vi.BitsPerComponent()) {
			case 8: return "420mpeg2";
			case 10: return "420p10";
			case 12: return "420p12";
			case 14: return "420p14";
			case 16: return "420p16";
			}
		}
		else if (vi.Is422()) {
			switch (vi.BitsPerComponent()) {
			case 8: return "422";
			case 10: return "422p10";
			case 12: return "422p12";
			case 14: return "422p14";
			case 16: return "422p16";
			}
		}
		else if (vi.Is444()) {
			switch (vi.BitsPerComponent()) {
			case 8: return "444";
			case 10: return "424p10";
			case 12: return "424p12";
			case 14: return "424p14";
			case 16: return "424p16";
			}
		}
		else if (vi.IsY()) {
			switch (vi.BitsPerComponent()) {
			case 8: return "mono";
			case 16: return "mono16";
			}
		}
		THROW(FormatException, "サポートされていないフィルタ出力形式です");
		return 0;
	}
public:
	Y4MWriter(VideoInfo vi, VideoFormat outfmt) : n(0) {
		StringBuilder sb;
		sb.append("YUV4MPEG2 W%d H%d C%s I%s F%d:%d A%d:%d",
			outfmt.width, outfmt.height,
			getPixelFormat(vi), outfmt.progressive ? "p" : "t",
			vi.fps_numerator, vi.fps_denominator,
			outfmt.sarWidth, outfmt.sarHeight);
		header = sb.str();
		header.push_back(0x0a);
		frameHeader = "FRAME";
		frameHeader.push_back(0x0a);
		nc = vi.IsY() ? 1 : 3;
	}
	void inputFrame(const PVideoFrame& frame) {
		if (n++ == 0) {
			buffer.add(MemoryChunk((uint8_t*)header.data(), header.size()));
		}
		buffer.add(MemoryChunk((uint8_t*)frameHeader.data(), frameHeader.size()));
		int yuv[] = { PLANAR_Y, PLANAR_U, PLANAR_V };
		for (int c = 0; c < nc; ++c) {
			const uint8_t* plane = frame->GetReadPtr(yuv[c]);
			int pitch = frame->GetPitch(yuv[c]);
			int height = frame->GetHeight(yuv[c]);
			int rowsize = frame->GetRowSize(yuv[c]);
			for (int y = 0; y < height; ++y) {
				buffer.add(MemoryChunk((uint8_t*)plane + y * pitch, rowsize));
			}
			onWrite(buffer.get());
			buffer.clear();
		}
	}
protected:
	virtual void onWrite(MemoryChunk mc) = 0;
private:
	int n;
	int nc;
	std::string header;
	std::string frameHeader;
	AutoBuffer buffer;
};

class Y4MEncodeWriter : AMTObject, NonCopyable
{
	static const char* getYUV(VideoInfo vi) {
		if (vi.Is420()) return "420";
		if (vi.Is422()) return "422";
		if (vi.Is444()) return "424";
		return "Unknown";
	}
public:
	Y4MEncodeWriter(AMTContext& ctx, const tstring& encoder_args, VideoInfo vi, VideoFormat fmt)
		: AMTObject(ctx)
		, y4mWriter_(new MyVideoWriter(this, vi, fmt))
		, process_(new StdRedirectedSubProcess(encoder_args, 5))
	{
		ctx.infoF("y4m format: YUV%sp%d %s %dx%d SAR %d:%d %d/%dfps",
			getYUV(vi), vi.BitsPerComponent(), fmt.progressive ? "progressive" : "tff",
			fmt.width, fmt.height, fmt.sarWidth, fmt.sarHeight, vi.fps_numerator, vi.fps_denominator);
	}
	~Y4MEncodeWriter()
	{
		if (process_->isRunning()) {
			THROW(InvalidOperationException, "call finish before destroy object ...");
		}
	}

	void inputFrame(const PVideoFrame& frame) {
		y4mWriter_->inputFrame(frame);
	}

	void finish() {
		if (y4mWriter_ != NULL) {
			process_->finishWrite();
			int ret = process_->join();
			if (ret != 0) {
				ctx.error("↓↓↓↓↓↓エンコーダ最後の出力↓↓↓↓↓↓");
				for (auto v : process_->getLastLines()) {
					v.push_back(0); // null terminate
					ctx.errorF("%s", v.data());
				}
				ctx.error("↑↑↑↑↑↑エンコーダ最後の出力↑↑↑↑↑↑");
				THROWF(RuntimeException, "エンコーダ終了コード: 0x%x", ret);
			}
		}
	}

	const std::deque<std::vector<char>>& getLastLines() {
		process_->getLastLines();
	}

private:
	class MyVideoWriter : public Y4MWriter {
	public:
		MyVideoWriter(Y4MEncodeWriter* this_, VideoInfo vi, VideoFormat fmt)
			: Y4MWriter(vi, fmt)
			, this_(this_)
		{ }
	protected:
		virtual void onWrite(MemoryChunk mc) {
			this_->onVideoWrite(mc);
		}
	private:
		Y4MEncodeWriter* this_;
	};

	std::unique_ptr<MyVideoWriter> y4mWriter_;
	std::unique_ptr<StdRedirectedSubProcess> process_;

	void onVideoWrite(MemoryChunk mc) {
		process_->write(mc);
	}
};

class AMTFilterVideoEncoder : public AMTObject {
public:
  AMTFilterVideoEncoder(
    AMTContext&ctx)
    : AMTObject(ctx)
    , thread_(this, 16)
  { }

  void encode(
    PClip source, VideoFormat outfmt, const std::vector<int> frameDurations,
    const std::vector<tstring>& encoderOptions,
    IScriptEnvironment* env)
  {
    vi_ = source->GetVideoInfo();
    outfmt_ = outfmt;

    int bufsize = outfmt_.width * outfmt_.height * 3;

    if (frameDurations.size() > 0 &&
      vi_.num_frames != std::accumulate(frameDurations.begin(), frameDurations.end(), 0))
    {
      THROW(RuntimeException, "フレーム数が合いません");
    }

    int npass = (int)encoderOptions.size();
    for (int i = 0; i < npass; ++i) {
      ctx.infoF("%d/%dパス エンコード開始 予定フレーム数: %d", i + 1, npass, 
        (frameDurations.size() > 0) ? frameDurations.size() : vi_.num_frames);

      const tstring& args = encoderOptions[i];

			ctx.info("[エンコーダ起動]");
			ctx.infoF("%s", args);

			// 初期化
      encoder_ = std::unique_ptr<Y4MEncodeWriter>(new Y4MEncodeWriter(ctx, args, vi_, outfmt_));

      Stopwatch sw;
      // エンコードスレッド開始
      thread_.start();
      sw.start();

      bool error = false;

      try {
        // エンコード
        for (int f = 0, i = 0; f < vi_.num_frames; ) {
          auto frame = source->GetFrame(f, env);
          thread_.put(std::unique_ptr<PVideoFrame>(new PVideoFrame(frame)), 1);
          f += (frameDurations.size() > 0) ? frameDurations[i++] : 1;
        }
      }
      catch (const AvisynthError& avserror) {
        ctx.errorF("Avisynthフィルタでエラーが発生: %s", avserror.msg);
        error = true;
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
      ctx.infoF("Total: %.2fs, FilterWait: %.2fs, EncoderWait: %.2fs", sw.getTotal(), prod, cons);
    }
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
			this_->encoder_->inputFrame(*data);
    }
  private:
    AMTFilterVideoEncoder * this_;
  };

  VideoInfo vi_;
  VideoFormat outfmt_;
  std::unique_ptr<Y4MEncodeWriter> encoder_;

  SpDataPumpThread thread_;
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
    AudioFileWriter(AVStream* stream, const tstring& filename, int bufsize)
      : AudioWriter(stream, bufsize)
      , file_(filename, _T("wb"))
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
    File file(setting_.getSrcFilePath(), _T("rb"));
    srcFileSize_ = file.size();
    double srcBitrate = ((double)srcFileSize_ * 8 / 1000) / (stream->duration * av_q2d(stream->time_base));
    ctx.infoF("入力映像ビットレート: %d kbps", (int)srcBitrate);

    if (setting_.isAutoBitrate()) {
      ctx.infoF("目標映像ビットレート: %d kbps",
        (int)setting_.getBitrate().getTargetBitrate(fmt.format, srcBitrate));
    }

    // 初期化
    tstring args = makeEncoderArgs(
      setting_.getEncoder(),
      setting_.getEncoderPath(),
      setting_.getOptions(
        0, fmt.format, srcBitrate, false, pass_, std::vector<BitrateZone>(), 1, 0, 0, CMTYPE_BOTH),
      fmt, tstring(), false,
      setting_.getEncVideoFilePath(0, 0, CMTYPE_BOTH));

    ctx.info("[エンコーダ開始]");
    ctx.infoF("%s", args);

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
