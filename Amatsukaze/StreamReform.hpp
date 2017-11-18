/**
* Output stream construction
* Copyright (c) 2017 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include <vector>
#include <map>
#include <memory>
#include <functional>

#include "StreamUtils.hpp"

// 時間は全て 90kHz double で計算する
// 90kHzでも60*1000/1001fpsの1フレームの時間は整数で表せない
// だからと言って27MHzでは数値が大きすぎる

struct FileAudioFrameInfo : public AudioFrameInfo {
	int audioIdx;
	int codedDataSize;
	int waveDataSize;
	int64_t fileOffset;
	int64_t waveOffset;

	FileAudioFrameInfo()
		: AudioFrameInfo()
		, audioIdx(0)
		, codedDataSize(0)
		, waveDataSize(0)
		, fileOffset(0)
		, waveOffset(-1)
	{ }

	FileAudioFrameInfo(const AudioFrameInfo& info)
		: AudioFrameInfo(info)
		, audioIdx(0)
		, codedDataSize(0)
		, waveDataSize(0)
		, fileOffset(0)
		, waveOffset(-1)
	{ }
};

struct FileVideoFrameInfo : public VideoFrameInfo {
  int64_t fileOffset;

  FileVideoFrameInfo()
    : VideoFrameInfo()
    , fileOffset(0)
  { }

  FileVideoFrameInfo(const VideoFrameInfo& info)
    : VideoFrameInfo(info)
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
	int frameIdx;	// フレーム番号
	int audioIdx;	// 変更された音声インデックス（AUDIO_FORMAT_CHANGEDのときのみ有効）
	int numAudio;	// 音声の数（PID_TABLE_CHANGEDのときのみ有効）
};

typedef std::vector<std::vector<int>> FileAudioFrameList;

struct OutVideoFormat {
	int formatId; // 内部フォーマットID（通し番号）
	int videoFileId;
	VideoFormat videoFormat;
	std::vector<AudioFormat> audioFormat;
};

// 音ズレ統計情報
struct AudioDiffInfo {
	double sumPtsDiff;
	int totalSrcFrames;
	int totalAudioFrames; // 出力した音声フレーム（水増し分を含む）
	int totalUniquAudioFrames; // 出力した音声フレーム（水増し分を含まず）
  double maxPtsDiff;
  double maxPtsDiffPos;
  double basePts;

	// 秒単位で取得
	double avgDiff() const {
		return ((double)sumPtsDiff / totalAudioFrames) / MPEG_CLOCK_HZ;
	}
	// 秒単位で取得
	double maxDiff() const {
		return (double)maxPtsDiff / MPEG_CLOCK_HZ;
	}

	void printAudioPtsDiff(AMTContext& ctx) const {
		double avgDiff = this->avgDiff() * 1000;
		double maxDiff = this->maxDiff() * 1000;
		int notIncluded = totalSrcFrames - totalUniquAudioFrames;

		ctx.info("出力音声フレーム: %d（うち水増しフレーム%d）",
			totalAudioFrames, totalAudioFrames - totalUniquAudioFrames);
		ctx.info("未出力フレーム: %d（%.3f%%）",
			notIncluded, (double)notIncluded * 100 / totalSrcFrames);

		ctx.info("音ズレ: 平均 %.2fms 最大 %.2fms",
			avgDiff, maxDiff);
		if (maxPtsDiff > 0 && maxDiff - avgDiff > 1) {
			ctx.info("最大音ズレ位置: 入力最初の映像フレームから%.3f秒後",
				elapsedTime(maxPtsDiffPos));
		}
	}

	void printToJson(std::ostringstream& ss) {
		double avgDiff = this->avgDiff() * 1000;
		double maxDiff = this->maxDiff() * 1000;
		int notIncluded = totalSrcFrames - totalUniquAudioFrames;
    double maxDiffPos = maxPtsDiff > 0 ? elapsedTime(maxPtsDiffPos) : 0.0;

		ss << "{ \"totalsrcframes\": " << totalSrcFrames 
			<< ", \"totaloutframes\": " << totalAudioFrames
			<< ", \"totaloutuniqueframes\": " << totalUniquAudioFrames
			<< ", \"notincludedper\": " << std::fixed << std::setprecision(3)
			<< ((double)notIncluded * 100 / totalSrcFrames)
			<< ", \"avgdiff\": " << std::fixed << std::setprecision(3) << avgDiff
			<< ", \"maxdiff\": " << std::fixed << std::setprecision(3) << maxDiff
      << ", \"maxdiffpos\": " << std::fixed << std::setprecision(3) << maxDiffPos
			<< " }";
	}

private:
	double elapsedTime(double modPTS) const {
		return (double)(modPTS - basePts) / MPEG_CLOCK_HZ;
	}
};

struct FilterSourceFrame {
  bool halfDelay;
  int frameIndex; // 内部用
  double pts; // 内部用
	double frameDuration; // 内部用
  int64_t framePTS;
  int64_t fileOffset;
  int keyFrame;
  CMType cmType;
};

struct FilterAudioFrame {
	int frameIndex; // デバッグ用
	int64_t waveOffset;
	int waveLength;
};

struct FilterOutVideoInfo {
  int numFrames;
  int frameRateNum;
  int frameRateDenom;
  int fakeAudioSampleRate;
  std::vector<int> fakeAudioSamples;
};

struct OutCaptionLine {
	double start, end;
	CaptionLine* line;
};

class StreamReformInfo : public AMTObject {
public:
	StreamReformInfo(
		AMTContext& ctx,
		int numVideoFile,
		std::vector<FileVideoFrameInfo>& videoFrameList,
		std::vector<FileAudioFrameInfo>& audioFrameList,
		std::vector<CaptionItem>& captionList,
		std::vector<StreamEvent>& streamEventList)
		: AMTObject(ctx)
		, numVideoFile_(numVideoFile)
		, videoFrameList_(std::move(videoFrameList))
		, audioFrameList_(std::move(audioFrameList))
		, captionItemList_(std::move(captionList))
		, streamEventList_(std::move(streamEventList))
		, isVFR_(false)
		, hasRFF_(false)
		, srcTotalDuration_()
		, outTotalDuration_()
	{ }

  // 1.
  AudioDiffInfo prepare() {
    reformMain();
    return genWaveAudioStream();
  }

	// 2.
  void applyCMZones(int videoFileIndex, const std::vector<EncoderZone>& cmzones) {
    auto& frames = filterFrameList_[videoFileIndex];
    for (auto zone : cmzones) {
      for (int i = zone.startFrame; i < zone.endFrame; ++i) {
        frames[i].cmType = CMTYPE_CM;
      }
    }
  }

	// 3.
  AudioDiffInfo genAudio() {
		calcSizeAndTime();
    return genAudioStream();
  }

	//AudioDiffInfo prepareEncode() {
	//	reformMain();
	//	genAudioStream();
	//	return genWaveAudioStream();
	//}

	int getNumVideoFile() const {
		return numVideoFile_;
	}

  VIDEO_STREAM_FORMAT getVideoStreamFormat() const {
    return videoFrameList_[0].format.format;
  }

  const std::vector<FilterSourceFrame>& getFilterSourceFrames(int videoFileIndex) const {
    return filterFrameList_[videoFileIndex];
  }

	const std::vector<FilterAudioFrame>& getFilterSourceAudioFrames(int videoFileIndex) const {
		return filterAudioFrameList_[videoFileIndex];
	}

	int getNumEncoders(int videoFileIndex) const {
		return int(
			outFormatStartIndex_[videoFileIndex + 1] - outFormatStartIndex_[videoFileIndex]);
	}

	int getNumOutFiles() const {
		return (int)outFormat_.size();
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

	const OutVideoFormat& getFormat(int encoderIndex, int videoFileIndex) const {
		int formatId = outFormatStartIndex_[videoFileIndex] + encoderIndex;
		return outFormat_[formatId];
  }

	int getOutFileIndex(int encoderIndex, int videoFileIndex) const {
		return outFormatStartIndex_[videoFileIndex] + encoderIndex;
	}

  // 映像データサイズ（バイト）、時間（タイムスタンプ）のペア
  std::pair<int64_t, double> getSrcVideoInfo(int encoderIndex, int videoFileIndex) const {
    int formatId = outFormatStartIndex_[videoFileIndex] + encoderIndex;
    return std::make_pair(fileSrcSize_[formatId], fileSrcDuration_[formatId]);
  }

	const FileAudioFrameList& getFileAudioFrameList(
		int encoderIndex, int videoFileIndex, CMType cmtype) const
	{
		int formatId = outFormatStartIndex_[videoFileIndex] + encoderIndex;
		return *reformedAudioFrameList_[formatId][cmtype].get();
	}

  // TODO: VFR用タイムコード取得
  // infps: フィルタ入力のFPS
  // outpfs: フィルタ出力のFPS
  void getTimeCode(
    int encoderIndex, int videoFileIndex, CMType cmtype, double infps, double outfps) const
  {
    //
  }

	// 各ファイルの再生時間
	double getFileDuration(int encoderIndex, int videoFileIndex, CMType cmtype) const {
		int formatId = outFormatStartIndex_[videoFileIndex] + encoderIndex;
		return fileDuration_[formatId][cmtype];
	}

	const std::vector<int64_t>& getAudioFileOffsets() const {
		return audioFileOffsets_;
	}

	const std::vector<int>& getOutFileMapping() const {
		return outFileIndex_;
	}

	bool isVFR() const {
		return isVFR_;
	}

	bool hasRFF() const {
		return hasRFF_;
	}

	std::pair<double, double> getInOutDuration() const {
		return std::make_pair(srcTotalDuration_, outTotalDuration_);
	}

	void printOutputMapping(std::function<std::string(int)> getFileName) const
	{
		ctx.info("[出力ファイル]");
		for (int i = 0; i < (int)outFormat_.size(); ++i) {
			ctx.info("%d: %s", i, getFileName(i).c_str());
		}

		ctx.info("[入力->出力マッピング]");
    double fromPTS = dataPTS_[0];
		int prevFormatId = 0;
		for (int i = 0; i < (int)ordredVideoFrame_.size(); ++i) {
			int ordered = ordredVideoFrame_[i];
      double pts = modifiedPTS_[ordered];
			int formatId = frameFormatId_[ordered];
			if (prevFormatId != formatId) {
				// print
				ctx.info("%8.3f秒 - %8.3f秒 -> %d",
					elapsedTime(fromPTS), elapsedTime(pts), outFileIndex_[prevFormatId]);
				prevFormatId = formatId;
				fromPTS = pts;
			}
		}
		ctx.info("%8.3f秒 - %8.3f秒 -> %d",
			elapsedTime(fromPTS), elapsedTime(dataPTS_.back()), outFileIndex_[prevFormatId]);
	}

	// 以下デバッグ用 //

	void serialize(const std::string& path) {
    serialize(File(path, "wb"));
	}

  void serialize(const File& file) {
    file.writeValue(numVideoFile_);
    file.writeArray(videoFrameList_);
    file.writeArray(audioFrameList_);
		WriteCaptions(file, captionItemList_);
    file.writeArray(streamEventList_);
  }

  static StreamReformInfo deserialize(AMTContext& ctx, const std::string& path) {
    return deserialize(ctx, File(path, "rb"));
  }

	static StreamReformInfo deserialize(AMTContext& ctx, const File& file) {
		int numVideoFile = file.readValue<int>();
		auto videoFrameList = file.readArray<FileVideoFrameInfo>();
		auto audioFrameList = file.readArray<FileAudioFrameInfo>();
		auto captionList = ReadCaptions(file);
		auto streamEventList = file.readArray<StreamEvent>();
		return StreamReformInfo(ctx,
			numVideoFile, videoFrameList, audioFrameList, captionList, streamEventList);
	}

private:

	struct CaptionDuration {
		int64_t startPTS, endPTS;
	};

	// 入力解析の出力
	int numVideoFile_;
	std::vector<FileVideoFrameInfo> videoFrameList_; // [DTS順] 
	std::vector<FileAudioFrameInfo> audioFrameList_;
	std::vector<CaptionItem> captionItemList_;
	std::vector<StreamEvent> streamEventList_;

	// 計算データ
	bool isVFR_;
	bool hasRFF_;
	std::vector<double> modifiedPTS_; // [DTS順] ラップアラウンドしないPTS
	std::vector<double> modifiedAudioPTS_; // ラップアラウンドしないPTS
	std::vector<double> modifiedCaptionPTS_; // ラップアラウンドしないPTS
	std::vector<double> audioFrameDuration_; // 各音声フレームの時間
	std::vector<int> ordredVideoFrame_; // [PTS順] -> [DTS順] 変換
	std::vector<double> dataPTS_; // [DTS順] 映像フレームのストリーム上での位置とPTSの関連付け
	std::vector<double> streamEventPTS_;
	std::vector<CaptionDuration> captionDuration_;

	std::vector<std::vector<int>> indexAudioFrameList_; // 音声インデックスごとのフレームリスト

	std::vector<OutVideoFormat> outFormat_;
	// 中間映像ファイルごとのフォーマット開始インデックス
	// サイズは中間映像ファイル数+1
  std::vector<int> outFormatStartIndex_;

  // 中間映像ファイルごと
	std::vector<std::vector<FilterSourceFrame>> filterFrameList_; // [PTS順]
	std::vector<std::vector<FilterAudioFrame>> filterAudioFrameList_;

	std::vector<int> frameFormatId_; // videoFrameList_と同じサイズ
	//std::map<int64_t, int> framePtsMap_;

  // 出力ファイルごとの入力映像データサイズ、時間
  std::vector<int64_t> fileSrcSize_;
  std::vector<double> fileSrcDuration_;
	std::vector<std::array<double, CMTYPE_MAX>> fileDuration_; // 各ファイルの再生時間

	// 2nd phase 出力
	//std::vector<bool> encodedFrames_;

	// 音声構築用
	std::vector<std::array<std::unique_ptr<FileAudioFrameList>, CMTYPE_MAX>> reformedAudioFrameList_;

	std::vector<int64_t> audioFileOffsets_; // 音声ファイルキャッシュ用
	std::vector<int> outFileIndex_; // 出力番号マッピング

  double srcTotalDuration_;
  double outTotalDuration_;

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

		/*
		// framePtsMap_を作成（すぐに作れるので）
		for (int i = 0; i < int(videoFrameList_.size()); ++i) {
			framePtsMap_[videoFrameList_[i].PTS] = i;
		}
		*/

		// VFR検出
		isVFR_ = false;
		for (int i = 0; i < int(videoFrameList_.size()); ++i) {
			if (videoFrameList_[i].format.fixedFrameRate == false) {
				isVFR_ = true;
				break;
			}
		}

		if (isVFR_) {
			THROW(FormatException, "このバージョンはVFRに対応していません");
		}

		// ラップアラウンドしないPTSを生成
		makeModifiedPTS(modifiedPTS_, videoFrameList_);
		makeModifiedPTS(modifiedAudioPTS_, audioFrameList_);
		makeModifiedPTS(modifiedCaptionPTS_, captionItemList_);

		// audioFrameDuration_を生成
		audioFrameDuration_.resize(audioFrameList_.size());
		for (int i = 0; i < (int)audioFrameList_.size(); ++i) {
			const auto& frame = audioFrameList_[i];
			audioFrameDuration_[i] = (frame.numSamples * MPEG_CLOCK_HZ) / (double)frame.format.sampleRate;
		}

		// ptsOrdredVideoFrame_を生成
		ordredVideoFrame_.resize(videoFrameList_.size());
		for (int i = 0; i < (int)videoFrameList_.size(); ++i) {
			ordredVideoFrame_[i] = i;
		}
		std::sort(ordredVideoFrame_.begin(), ordredVideoFrame_.end(), [&](int a, int b) {
			return modifiedPTS_[a] < modifiedPTS_[b];
		});

		// dataPTSを生成
		// 後ろから見てその時点で最も小さいPTSをdataPTSとする
		double curMin = INFINITY;
    double curMax = 0;
		dataPTS_.resize(videoFrameList_.size());
		for (int i = (int)videoFrameList_.size() - 1; i >= 0; --i) {
			curMin = std::min(curMin, modifiedPTS_[i]);
			curMax = std::max(curMax, modifiedPTS_[i]);
			dataPTS_[i] = curMin;
		}

		// 字幕の開始・終了を計算
		double curEnd = dataPTS_.back();
		for (int i = (int)captionItemList_.size() - 1; i >= 0; --i) {
			int64_t modPTS = modifiedCaptionPTS_[i] + (captionItemList_[i].waitTime * (MPEG_CLOCK_HZ / 1000));
			if (captionItemList_[i].line) {
				captionDuration_[i].startPTS = modPTS;
				captionDuration_[i].endPTS = curEnd;
			}
			else {
				// クリア
				captionDuration_[i].startPTS = captionDuration_[i].endPTS = modPTS;
				// 終了を更新
				curEnd = modPTS;
			}
		}

		// ストリームイベントのPTSを計算
    double endPTS = curMax + 1;
		streamEventPTS_.resize(streamEventList_.size());
		for (int i = 0; i < (int)streamEventList_.size(); ++i) {
			auto& ev = streamEventList_[i];
      double pts = -1;
			if (ev.type == PID_TABLE_CHANGED || ev.type == VIDEO_FORMAT_CHANGED) {
				if (ev.frameIdx >= (int)videoFrameList_.size()) {
					// 後ろ過ぎて対象のフレームがない
					pts = endPTS;
				}
				else {
					pts = dataPTS_[ev.frameIdx];
				}
			}
			else if (ev.type == AUDIO_FORMAT_CHANGED) {
				if (ev.frameIdx >= (int)audioFrameList_.size()) {
					// 後ろ過ぎて対象のフレームがない
					pts = endPTS;
				}
				else {
					pts = modifiedAudioPTS_[ev.frameIdx];
				}
			}
			streamEventPTS_[i] = pts;
		}

		// 時間的に近いストリームイベントを1つの変化点とみなす
		const double CHANGE_TORELANCE = 3 * MPEG_CLOCK_HZ;

		std::vector<int> sectionFormatList;
		std::vector<double> startPtsList;

		ctx.info("[フォーマット切り替え解析]");

		OutVideoFormat curFormat = OutVideoFormat();
    double startPts = -1;
    double curFromPTS = -1;
		curFormat.videoFileId = -1;
		for (int i = 0; i < (int)streamEventList_.size(); ++i) {
			auto& ev = streamEventList_[i];
      double pts = streamEventPTS_[i];
			if (pts >= endPTS) {
				// 後ろに映像がなければ意味がない
				continue;
			}
			if (curFromPTS != -1 && curFromPTS + CHANGE_TORELANCE < pts) {
				// 区間を追加
				registerOrGetFormat(curFormat);
				sectionFormatList.push_back(curFormat.formatId);
				startPtsList.push_back(curFromPTS);
				if (startPts == -1) {
					startPts = curFromPTS;
				}
				ctx.info("%.2f -> %d", (curFromPTS - startPts) / 90000.0, curFormat.formatId);
				curFromPTS = -1;
			}
			// 変更を反映
			switch (ev.type) {
			case PID_TABLE_CHANGED:
				if (curFormat.audioFormat.size() != ev.numAudio) {
					curFormat.audioFormat.resize(ev.numAudio);
					if (curFromPTS == -1) {
						curFromPTS = pts;
					}
				}
				break;
			case VIDEO_FORMAT_CHANGED:
				// ファイル変更
				++curFormat.videoFileId;
				outFormatStartIndex_.push_back((int)outFormat_.size());
				curFormat.videoFormat = videoFrameList_[ev.frameIdx].format;
				// 映像フォーマットの変更時刻を優先させる
				curFromPTS = dataPTS_[ev.frameIdx];
				break;
			case AUDIO_FORMAT_CHANGED:
				if (ev.audioIdx >= (int)curFormat.audioFormat.size()) {
					THROW(FormatException, "StreamEvent's audioIdx exceeds numAudio of the previous table change event");
				}
				curFormat.audioFormat[ev.audioIdx] = audioFrameList_[ev.frameIdx].format;
				if (curFromPTS == -1) {
					curFromPTS = pts;
				}
				break;
			}
		}
		// 最後の区間を追加
		if (curFromPTS != -1) {
			registerOrGetFormat(curFormat);
			sectionFormatList.push_back(curFormat.formatId);
			startPtsList.push_back(curFromPTS);
			if (startPts == -1) {
				startPts = curFromPTS;
			}
			ctx.info("%.2f -> %d", (curFromPTS - startPts) / 90000.0, curFormat.formatId);
		}
		startPtsList.push_back(endPTS);
		outFormatStartIndex_.push_back((int)outFormat_.size());

		// frameFormatId_を生成
		frameFormatId_.resize(videoFrameList_.size());
		for (int i = 0; i < int(videoFrameList_.size()); ++i) {
      double pts = modifiedPTS_[i];
			// 区間を探す
			int sectionId = int(std::partition_point(startPtsList.begin(), startPtsList.end(),
				[=](double sec) {
				return !(pts < sec);
			}) - startPtsList.begin() - 1);
			if (sectionId >= (int)sectionFormatList.size()) {
				THROWF(RuntimeException, "sectionId exceeds section count (%d >= %d) at frame %d",
					sectionId, (int)sectionFormatList.size(), i);
			}
			int fmtid = sectionFormatList[sectionId];
      frameFormatId_[i] = fmtid;
    }

    // フィルタ用入力フレームリスト生成
    filterFrameList_ = std::vector<std::vector<FilterSourceFrame>>(numVideoFile_);
    for (int fileId = 0; fileId < (int)numVideoFile_; ++fileId) {
      int keyFrame = -1;
      std::vector<FilterSourceFrame>& list = filterFrameList_[fileId];

      auto& format = outFormat_[outFormatStartIndex_[fileId]].videoFormat;
      double timePerFrame = format.frameRateDenom * MPEG_CLOCK_HZ / (double)format.frameRateNum;

      for (int i = 0; i < (int)videoFrameList_.size(); ++i) {
        int ordered = ordredVideoFrame_[i];
        int formatId = frameFormatId_[ordered];
        if (outFormat_[formatId].videoFileId == fileId) {

					int64_t mPTS = modifiedPTS_[ordered];
          FileVideoFrameInfo& srcframe = videoFrameList_[ordered];
          if (srcframe.isGopStart) {
            keyFrame = int(list.size());
          }

          // まだキーフレームがない場合は捨てる
          if (keyFrame == -1) continue;

          FilterSourceFrame frame;
          frame.halfDelay = false;
          frame.frameIndex = i;
          frame.pts = (double)mPTS;
					frame.frameDuration = timePerFrame; // TODO: VFR対応
          frame.framePTS = mPTS;
          frame.fileOffset = srcframe.fileOffset;
          frame.keyFrame = keyFrame;
          frame.cmType = CMTYPE_NONCM; // 最初は全部NonCMにしておく

          switch (srcframe.pic) {
          case PIC_FRAME:
          case PIC_TFF:
          case PIC_TFF_RFF:
            list.push_back(frame);
            break;
          case PIC_FRAME_DOUBLING:
            list.push_back(frame);
            frame.pts += timePerFrame;
            list.push_back(frame);
            break;
          case PIC_FRAME_TRIPLING:
            list.push_back(frame);
            frame.pts += timePerFrame;
            list.push_back(frame);
            frame.pts += timePerFrame;
            list.push_back(frame);
            break;
          case PIC_BFF:
            frame.halfDelay = true;
            frame.pts -= timePerFrame / 2;
            list.push_back(frame);
            break;
          case PIC_BFF_RFF:
            frame.halfDelay = true;
            frame.pts -= timePerFrame / 2;
            list.push_back(frame);
            frame.halfDelay = false;
            frame.pts += timePerFrame;
            list.push_back(frame);
            break;
          }
        }
      }
    }

		// indexAudioFrameList_を作成
		int numMaxAudio = 1;
		for (int i = 0; i < (int)outFormat_.size(); ++i) {
			numMaxAudio = std::max(numMaxAudio, (int)outFormat_[i].audioFormat.size());
		}
		indexAudioFrameList_.resize(numMaxAudio);
		for (int i = 0; i < (int)audioFrameList_.size(); ++i) {
			indexAudioFrameList_[audioFrameList_[i].audioIdx].push_back(i);
		}

		// audioFileOffsets_を生成
		audioFileOffsets_.resize(audioFrameList_.size() + 1);
		for (int i = 0; i < (int)audioFrameList_.size(); ++i) {
			audioFileOffsets_[i] = audioFrameList_[i].fileOffset;
		}
		const auto& lastFrame = audioFrameList_.back();
		audioFileOffsets_.back() = lastFrame.fileOffset + lastFrame.codedDataSize;
	}

	void calcSizeAndTime()
	{
		// 各ファイルの入力ファイル時間とサイズを計算
		// ソースフレームから
		fileSrcSize_ = std::vector<int64_t>(outFormat_.size(), 0);
		fileSrcDuration_ = std::vector<double>(outFormat_.size(), 0);
		for (int i = 0; i < (int)videoFrameList_.size(); ++i) {
			int ordered = ordredVideoFrame_[i];
			int formatId = frameFormatId_[ordered];
			int next = (i + 1 < (int)videoFrameList_.size())
				? ordredVideoFrame_[i + 1]
				: -1;
			double duration = getSourceFrameDuration(ordered, next);

			const auto& frame = videoFrameList_[ordered];
			fileSrcSize_[formatId] += frame.codedDataSize;
			fileSrcDuration_[formatId] += duration;
		}

		// 各ファイルの出力ファイル時間を計算
		// CM判定はフィルタ入力フレームに適用されているのでフィルタ入力フレームから
		fileDuration_ = std::vector<std::array<double, CMTYPE_MAX>>(outFormat_.size(), std::array<double, CMTYPE_MAX>());
		for (int fileId = 0; fileId < (int)numVideoFile_; ++fileId) {
			std::vector<FilterSourceFrame>& list = filterFrameList_[fileId];
			for (int i = 0; i < (int)list.size(); ++i) {
				int formatId = frameFormatId_[list[i].frameIndex];
				double duration = list[i].frameDuration;
				fileDuration_[formatId][CMTYPE_BOTH] += duration;
				fileDuration_[formatId][list[i].cmType] += duration;
			}
		}

		// 統計
		double sumDuration = 0;
		double maxDuration = 0;
		int maxId = 0;
		for (int i = 0; i < (int)outFormat_.size(); ++i) {
			double time = fileDuration_[i][CMTYPE_BOTH];
			sumDuration += time;
			if (maxDuration < time) {
				maxDuration = time;
				maxId = i;
			}
		}
		srcTotalDuration_ = dataPTS_.back() - dataPTS_.front();
		outTotalDuration_ = sumDuration;

		// 出力ファイル番号生成
		outFileIndex_.resize(outFormat_.size());
		outFileIndex_[maxId] = 0;
		for (int i = 0, cnt = 1; i < (int)outFormat_.size(); ++i) {
			if (i != maxId) {
				outFileIndex_[i] = cnt++;
			}
		}
	}

	template<typename I>
	void makeModifiedPTS(std::vector<double>& modifiedPTS, const std::vector<I>& frames)
	{
		// 前後のフレームのPTSに6時間以上のずれがあると正しく処理できない

		// ラップアラウンドしないPTSを生成
		modifiedPTS.resize(frames.size());
		int64_t prevPTS = frames[0].PTS;
		for (int i = 0; i < int(frames.size()); ++i) {
			int64_t PTS = frames[i].PTS;
			if (PTS == -1) {
				// PTSがない
				THROWF(FormatException,
					"PTSがありません。処理できません。 %dフレーム目", i);
			}
			int64_t modPTS = prevPTS + int64_t((int32_t(PTS) - int32_t(prevPTS)));
			modifiedPTS[i] = (double)modPTS;
			prevPTS = modPTS;
		}

		// ストリームが戻っている場合は処理できないのでエラーとする
		for (int i = 1; i < int(frames.size()); ++i) {
			if (modifiedPTS[i] - modifiedPTS[i - 1] < -60 * MPEG_CLOCK_HZ) {
				// 1分以上戻っている
				ctx.incrementCounter("incident");
				ctx.warn("PTSが戻っています。正しく処理できないかもしれません。 [%d] %.0f -> %.0f",
					i, modifiedPTS[i - 1], modifiedPTS[i]);
			}
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

	struct AudioState {
    double time = 0; // 追加された音声フレームの合計時間
		double lostPts = -1; // 同期ポイントを見失ったPTS（表示用）
		int lastFrame = -1;
	};

	struct OutFileState {
		int formatId; // デバッグ出力用
    double time; // 追加された映像フレームの合計時間
		std::vector<AudioState> audioState;
		std::unique_ptr<FileAudioFrameList> audioFrameList;
	};

  AudioDiffInfo initAudioDiffInfo() {
    AudioDiffInfo adiff = AudioDiffInfo();
    adiff.totalSrcFrames = (int)audioFrameList_.size();
    adiff.basePts = dataPTS_[0];
    return adiff;
  }

  // フィルタ入力から音声構築
  AudioDiffInfo genAudioStream()
  {
		AudioDiffInfo adiff = initAudioDiffInfo();

		std::vector<std::array<OutFileState, CMTYPE_MAX>> outFiles(outFormat_.size());

		// outFiles初期化
		for (int i = 0; i < (int)outFormat_.size(); ++i) {
			for (int c = 0; c < CMTYPE_MAX; ++c) {
				auto& file = outFiles[i][c];
				int numAudio = (int)outFormat_[i].audioFormat.size();
				file.formatId = i;
				file.time = 0;
				file.audioState.resize(numAudio);
				file.audioFrameList =
					std::unique_ptr<FileAudioFrameList>(new FileAudioFrameList(numAudio));
			}
		}

    // 全映像フレームを追加
    ctx.info("[音声構築]");
    for (int fileId = 0; fileId < numVideoFile_; ++fileId) {
      auto& frames = filterFrameList_[fileId];
			auto& format = outFormat_[outFormatStartIndex_[fileId]].videoFormat;
			double timePerFrame = format.frameRateDenom * MPEG_CLOCK_HZ / (double)format.frameRateNum;
      for (int i = 0; i < (int)frames.size(); ++i) {
				int ordered = ordredVideoFrame_[frames[i].frameIndex];
				int formatId = frameFormatId_[ordered];
        addVideoFrame(outFiles[formatId][frames[i].cmType], formatId, frames[i].pts, timePerFrame, nullptr);
        addVideoFrame(outFiles[formatId][CMTYPE_BOTH], formatId, frames[i].pts, timePerFrame, &adiff);
      }
    }

		// 出力データ生成
		reformedAudioFrameList_.resize(outFormat_.size());
		for (int i = 0; i < (int)outFormat_.size(); ++i) {
			for (int c = 0; c < CMTYPE_MAX; ++c) {
				double time = outFiles[i][c].time;
				reformedAudioFrameList_[i][c] = std::move(outFiles[i][c].audioFrameList);
			}
		}

    return adiff;
  }

  AudioDiffInfo genWaveAudioStream()
	{
    AudioDiffInfo adiff = initAudioDiffInfo();

		// 全映像フレームを追加
		ctx.info("[CM判定用音声構築]");
		filterAudioFrameList_.resize(numVideoFile_);
		for (int fileId = 0; fileId < (int)numVideoFile_; ++fileId) {
			OutFileState file = { 0 };
			file.formatId = -1;
			file.time = 0;
			file.audioState.resize(1);
			file.audioFrameList =
				std::unique_ptr<FileAudioFrameList>(new FileAudioFrameList(1));

			auto& frames = filterFrameList_[fileId];
			auto& format = outFormat_[outFormatStartIndex_[fileId]];
			double timePerFrame = format.videoFormat.frameRateDenom * MPEG_CLOCK_HZ / (double)format.videoFormat.frameRateNum;

			for (int i = 0; i < (int)frames.size(); ++i) {
				double endPts = frames[i].pts + timePerFrame;
				file.time += timePerFrame;

				// file.timeまで音声を進める
				auto& audioState = file.audioState[0];
				if (audioState.time < file.time) {
					double audioDuration = file.time - audioState.time;
					double audioPts = endPts - audioDuration;
					// ステレオに変換されているはずなので、音声フォーマットは問わない
					fillAudioFrames(file, 0, nullptr, audioPts, audioDuration, &adiff);
				}
			}

			auto& list = file.audioFrameList->at(0);
			for (int i = 0; i < (int)list.size(); ++i) {
				FilterAudioFrame frame = { 0 };
				auto& info = audioFrameList_[list[i]];
				frame.frameIndex = list[i];
				frame.waveOffset = info.waveOffset;
				frame.waveLength = info.waveDataSize;
				filterAudioFrameList_[fileId].push_back(frame);
			}
		}

    return adiff;
	}

	// フレームの表示時間
	// RFF: true=ソースフレームの表示時間 false=フィルタ入力フレーム（RFF処理済み）の表示時間
	template <bool RFF>
  double getFrameDuration(int index, int nextIndex)
  {
    const auto& videoFrame = videoFrameList_[index];
    int formatId = frameFormatId_[index];
    const auto& format = outFormat_[formatId];
    double frameDiff = format.videoFormat.frameRateDenom * MPEG_CLOCK_HZ / (double)format.videoFormat.frameRateNum;

    double duration;
    if (isVFR_) { // VFR
      if (nextIndex == -1) {
        duration = 0; // 最後のフレーム
      }
      else {
        duration = modifiedPTS_[nextIndex] - modifiedPTS_[index];
      }
    }
    else { // CFR
			if (RFF) {
				switch (videoFrame.pic) {
				case PIC_FRAME:
				case PIC_TFF:
					duration = frameDiff;
					break;
				case PIC_TFF_RFF:
					duration = frameDiff * 1.5;
					break;
				case PIC_FRAME_DOUBLING:
					duration = frameDiff * 2;
					hasRFF_ = true;
					break;
				case PIC_FRAME_TRIPLING:
					duration = frameDiff * 3;
					hasRFF_ = true;
					break;
				case PIC_BFF:
					duration = frameDiff;
					break;
				case PIC_BFF_RFF:
					duration = frameDiff * 1.5;
					hasRFF_ = true;
					break;
				}
			}
			else {
				duration = frameDiff;
			}
    }

    return duration;
  }

	// ソースフレームの表示時間
	double getSourceFrameDuration(int index, int nextIndex) {
		return getFrameDuration<true>(index, nextIndex);
	}

	// フィルタ入力フレーム（RFF処理済み）の表示時間
	double getFilterSourceFrameDuration(int index, int nextIndex) {
		return getFrameDuration<false>(index, nextIndex);
	}

  /*
	// ファイルに映像フレームを１枚追加
	// nextIndexはソース動画においてPTSで次のフレームの番号
  void addVideoFrame(OutFileState& file, int index, int nextIndex) {
    int formatId = frameFormatId_[index];

    double pts = modifiedPTS_[index];
    double duration = getFrameDuration(index, nextIndex);

    addVideoFrame(file, formatId, pts, duration);
	}
  */
  void addVideoFrame(OutFileState& file, int formatId, double pts, double duration, AudioDiffInfo* adiff) {
    const auto& format = outFormat_[formatId];
    double endPts = pts + duration;
    file.time += duration;

    ASSERT(format.audioFormat.size() == file.audioFrameList->size());
    ASSERT(format.audioFormat.size() == file.audioState.size());
    for (int i = 0; i < (int)format.audioFormat.size(); ++i) {
      // file.timeまで音声を進める
      auto& audioState = file.audioState[i];
      if (audioState.time >= file.time) {
        // 音声は十分進んでる
        continue;
      }
      double audioDuration = file.time - audioState.time;
      double audioPts = endPts - audioDuration;
      fillAudioFrames(file, i, &format.audioFormat[i], audioPts, audioDuration, adiff);
    }
  }

	void fillAudioFrames(
		OutFileState& file, int index, // 対象ファイルと音声インデックス
		const AudioFormat* format, // 音声フォーマット
    double pts, double duration, // 開始修正PTSと90kHzでのタイムスパン
    AudioDiffInfo* adiff)
	{
		auto& state = file.audioState[index];
		auto& outFrameList = file.audioFrameList->at(index);
		const auto& frameList = indexAudioFrameList_[index];

		fillAudioFramesInOrder(file, index, format, pts, duration, adiff);
		if (duration <= 0) {
			// 十分出力した
			return;
		}

		// もしかしたら戻ったらあるかもしれないので探しなおす
		auto it = std::partition_point(frameList.begin(), frameList.end(), [&](int frameIndex) {
      double modPTS = modifiedAudioPTS_[frameIndex];
			double frameDuration = audioFrameDuration_[frameIndex];
			return modPTS + (frameDuration / 2) < pts;
		});
		if (it != frameList.end()) {
			// 見つけたところに位置をセットして入れてみる
			if (state.lostPts != pts) {
				state.lostPts = pts;
				if (adiff) {
					ctx.debug("%.3f秒で音声%d-%dの同期ポイントを見失ったので再検索",
						elapsedTime(pts), file.formatId, index);
				}
			}
			state.lastFrame = (int)(it - frameList.begin() - 1);
			fillAudioFramesInOrder(file, index, format, pts, duration, adiff);
		}

		// 有効な音声フレームが見つからなかった場合はとりあえず何もしない
		// 次に有効な音声フレームが見つかったらその間はフレーム水増しされる
		// 映像より音声が短くなる可能性はあるが、有効な音声がないのであれば仕方ないし
		// 音ズレするわけではないので問題ないと思われる

	}

	// lastFrameから順番に見て音声フレームを入れる
	void fillAudioFramesInOrder(
		OutFileState& file, int index, // 対象ファイルと音声インデックス
		const AudioFormat* format, // 音声フォーマット
    double& pts, double& duration, // 開始修正PTSと90kHzでのタイムスパン
    AudioDiffInfo* adiff)
	{
		auto& state = file.audioState[index];
		auto& outFrameList = file.audioFrameList->at(index);
		const auto& frameList = indexAudioFrameList_[index];
		int nskipped = 0;

		for (int i = state.lastFrame + 1; i < (int)frameList.size(); ++i) {
			int frameIndex = frameList[i];
			const auto& frame = audioFrameList_[frameIndex];
      double modPTS = modifiedAudioPTS_[frameIndex];
			double frameDuration = audioFrameDuration_[frameIndex];
      double halfDuration = frameDuration / 2;
      double quaterDuration = frameDuration / 4;

			if (modPTS >= pts + duration) {
				// 開始が終了より後ろの場合
				if (modPTS >= pts + frameDuration - quaterDuration) {
					// フレームの4分の3以上のズレている場合
					// 行き過ぎ
					break;
				}
			}
			if (modPTS + (frameDuration / 2) < pts) {
				// 前すぎるのでスキップ
				++nskipped;
				continue;
			}
			if (format != nullptr && frame.format != *format) {
				// フォーマットが違うのでスキップ
				continue;
			}

			// 空きがある場合はフレームを水増しする
			// フレームの4分の3以上の空きができる場合は埋める
			int nframes = (int)std::max(1.0, ((modPTS - pts) + (frameDuration / 4)) / frameDuration);

			if (adiff) {
				if (nframes > 1) {
					ctx.debug("%.3f秒で音声%d-%dにずれがあるので%dフレーム水増し",
						elapsedTime(modPTS), file.formatId, index, nframes - 1);
				}
				if (nskipped > 0) {
					if (state.lastFrame == -1) {
						ctx.debug("音声%d-%dは%dフレーム目から開始",
							file.formatId, index, nskipped);
					}
					else {
						ctx.debug("%.3f秒で音声%d-%dにずれがあるので%dフレームスキップ",
							elapsedTime(modPTS), file.formatId, index, nskipped);
					}
					nskipped = 0;
				}

				++adiff->totalUniquAudioFrames;
			}

			for (int t = 0; t < nframes; ++t) {
				// 統計情報
				if (adiff) {
					double diff = std::abs(modPTS - pts);
					if (adiff->maxPtsDiff < diff) {
						adiff->maxPtsDiff = diff;
						adiff->maxPtsDiffPos = pts;
					}
					adiff->sumPtsDiff += diff;
					++adiff->totalAudioFrames;
				}

				// フレームを出力
				outFrameList.push_back(frameIndex);
				state.time += frameDuration;
				pts += frameDuration;
				duration -= frameDuration;
			}

			state.lastFrame = i;
			if (duration <= 0) {
				// 十分出力した
				return;
			}
		}
	}

	double elapsedTime(double modPTS) const {
		return (double)(modPTS - dataPTS_[0]) / MPEG_CLOCK_HZ;
	}
};

