/**
* Output stream construction
* Copyright (c) 2017-2019 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include <time.h>

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

		ctx.infoF("出力音声フレーム: %d（うち水増しフレーム%d）",
			totalAudioFrames, totalAudioFrames - totalUniquAudioFrames);
		ctx.infoF("未出力フレーム: %d（%.3f%%）",
			notIncluded, (double)notIncluded * 100 / totalSrcFrames);

		ctx.infoF("音ズレ: 平均 %.2fms 最大 %.2fms",
			avgDiff, maxDiff);
		if (maxPtsDiff > 0 && maxDiff - avgDiff > 1) {
			double sec = elapsedTime(maxPtsDiffPos);
			int minutes = (int)(sec / 60);
			sec -= minutes * 60;
			ctx.infoF("最大音ズレ位置: 入力最初の映像フレームから%d分%.3f秒後",
				minutes, sec);
		}
	}

	void printToJson(StringBuilder& sb) {
		double avgDiff = this->avgDiff() * 1000;
		double maxDiff = this->maxDiff() * 1000;
		int notIncluded = totalSrcFrames - totalUniquAudioFrames;
		double maxDiffPos = maxPtsDiff > 0 ? elapsedTime(maxPtsDiffPos) : 0.0;

		sb.append(
			"{ \"totalsrcframes\": %d, \"totaloutframes\": %d, \"totaloutuniqueframes\": %d, "
			"\"notincludedper\": %g, \"avgdiff\": %g, \"maxdiff\": %g, \"maxdiffpos\": %g }",
			totalSrcFrames, totalAudioFrames, totalUniquAudioFrames,
			(double)notIncluded * 100 / totalSrcFrames, avgDiff, maxDiff, maxDiffPos);
	}

private:
	double elapsedTime(double modPTS) const {
		return (double)(modPTS - basePts) / MPEG_CLOCK_HZ;
	}
};

struct FilterSourceFrame {
	bool halfDelay;
	int frameIndex; // 内部用(DTS順フレーム番号)
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

typedef std::vector<std::vector<OutCaptionLine>> OutCaptionList;

struct NicoJKLine {
	double start, end;
	std::string line;

	void Write(const File& file) const {
		file.writeValue(start);
		file.writeValue(end);
		file.writeString(line);
	}

	static NicoJKLine Read(const File& file) {
		NicoJKLine item;
		item.start = file.readValue<double>();
		item.end = file.readValue<double>();
		item.line = file.readString();
		return item;
	}
};

typedef std::array<std::vector<NicoJKLine>, NICOJK_MAX> NicoJKList;

typedef std::pair<int64_t, JSTTime> TimeInfo;

struct EncodeFileInput {
	EncodeFileKey key;     // キー
	EncodeFileKey outKey; // 出力ファイル名用キー
	EncodeFileKey keyMax;  // 出力ファイル名決定用最大値
	double duration;       // 再生時間
	std::vector<int> videoFrames; // 映像フレームリスト（中身はフィルタ入力フレームでのインデックス）
	FileAudioFrameList audioFrames; // 音声フレームリスト
	OutCaptionList captionList;     // 字幕
	NicoJKList nicojkList;          // ニコニコ実況コメント
};

class StreamReformInfo : public AMTObject {
public:
	StreamReformInfo(
		AMTContext& ctx,
		int numVideoFile,
		std::vector<FileVideoFrameInfo>& videoFrameList,
		std::vector<FileAudioFrameInfo>& audioFrameList,
		std::vector<CaptionItem>& captionList,
		std::vector<StreamEvent>& streamEventList,
		std::vector<TimeInfo>& timeList)
		: AMTObject(ctx)
		, numVideoFile_(numVideoFile)
		, videoFrameList_(std::move(videoFrameList))
		, audioFrameList_(std::move(audioFrameList))
		, captionItemList_(std::move(captionList))
		, streamEventList_(std::move(streamEventList))
		, timeList_(std::move(timeList))
		, isVFR_(false)
		, hasRFF_(false)
		, srcTotalDuration_()
		, outTotalDuration_()
		, firstFrameTime_()
	{ }

	// 1. コンストラクト直後に呼ぶ
	// splitSub: メイン以外のフォーマットを結合しない
	void prepare(bool splitSub) {
		reformMain(splitSub);
		genWaveAudioStream();
	}

	time_t getFirstFrameTime() const {
		return firstFrameTime_;
	}

	// 2. ニコニコ実況コメントを取得したら呼ぶ
	void SetNicoJKList(const std::array<std::vector<NicoJKLine>, NICOJK_MAX>& nicoJKList) {
		for (int t = 0; t < NICOJK_MAX; ++t) {
			nicoJKList_[t].resize(nicoJKList[t].size());
			double startTime = dataPTS_.front();
			for (int i = 0; i < (int)nicoJKList[t].size(); ++i) {
				auto& src = nicoJKList[t][i];
				auto& dst = nicoJKList_[t][i];
				// 開始映像オフセットを加算
				dst.start = src.start + startTime;
				dst.end = src.end + startTime;
				dst.line = src.line;
			}
		}
	}

	// 2. 各中間映像ファイルのCM解析後に呼ぶ
	// cmzones: CMゾーン（フィルタ入力フレーム番号）
	// divs: 分割ポイントリスト（フィルタ入力フレーム番号）
	void applyCMZones(int videoFileIndex, const std::vector<EncoderZone>& cmzones, const std::vector<int>& divs) {
		auto& frames = filterFrameList_[videoFileIndex];
		for (auto zone : cmzones) {
			for (int i = zone.startFrame; i < zone.endFrame; ++i) {
				frames[i].cmType = CMTYPE_CM;
			}
		}
		fileDivs_[videoFileIndex] = divs;
	}

	// 3. CM解析が終了したらエンコード前に呼ぶ
	// cmtypes: 出力するCMタイプリスト
	AudioDiffInfo genAudio(const std::vector<CMType>& cmtypes) {
		calcSizeAndTime(cmtypes);
		genCaptionStream();
		return genAudioStream();
	}

	// 中間映像ファイルの個数
	int getNumVideoFile() const {
		return numVideoFile_;
	}

	// 入力映像規格
	VIDEO_STREAM_FORMAT getVideoStreamFormat() const {
		return videoFrameList_[0].format.format;
	}

	// PMT変更PTSリスト
	std::vector<int> getPidChangedList(int videoFileIndex) const {
		std::vector<int> ret;
		auto& frames = filterFrameList_[videoFileIndex];
		for (int i = 0; i < (int)streamEventList_.size(); ++i) {
			if (streamEventList_[i].type == PID_TABLE_CHANGED) {
				FilterSourceFrame tmp = FilterSourceFrame();
				tmp.pts = streamEventPTS_[i];
				auto idx = std::lower_bound(frames.begin(), frames.end(), tmp,
					[&](const FilterSourceFrame& e, const FilterSourceFrame& value) {
					return dataPTS_[e.frameIndex] < value.pts;
				}) - frames.begin();
				if (ret.size() == 0 || ret.back() != idx) {
					ret.push_back((int)idx);
				}
			}
		}
		return ret;
	}

	int getMainVideoFileIndex() const {
		int maxFrames = 0, maxIndex = 0;
		for (int i = 0; i < (int)filterFrameList_.size(); ++i) {
			if (maxFrames < filterFrameList_[i].size()) {
				maxFrames = (int)filterFrameList_[i].size();
				maxIndex = i;
			}
		}
		return maxIndex;
	}

	// フィルタ入力映像フレーム
	const std::vector<FilterSourceFrame>& getFilterSourceFrames(int videoFileIndex) const {
		return filterFrameList_[videoFileIndex];
	}

	// フィルタ入力音声フレーム
	const std::vector<FilterAudioFrame>& getFilterSourceAudioFrames(int videoFileIndex) const {
		return filterAudioFrameList_[videoFileIndex];
	}

	// 出力ファイル情報
	const EncodeFileInput& getEncodeFile(EncodeFileKey key) const {
		return outFiles_.at(key.key());
	}

	// 中間一時ファイルごとの出力ファイル数
	int getNumEncoders(int videoFileIndex) const {
		return int(
			fileFormatStartIndex_[videoFileIndex + 1] - fileFormatStartIndex_[videoFileIndex]);
	}

	// 合計出力ファイル数
	//int getNumOutFiles() const {
	//	return (int)fileFormatId_.size();
	//}

	// video frame index -> VideoFrameInfo
	const VideoFrameInfo& getVideoFrameInfo(int frameIndex) const {
		return videoFrameList_[frameIndex];
	}

	// video frame index (DTS順) -> encoder index
	int getEncoderIndex(int frameIndex) const {
		int fileId = frameFormatId_[frameIndex];
		const auto& format = format_[fileFormatId_[fileId]];
		return fileId - formatStartIndex_[format.videoFileId];
	}

	// keyはvideo,formatの2つしか使われない
	const OutVideoFormat& getFormat(EncodeFileKey key) const {
		int fileId = fileFormatStartIndex_[key.video] + key.format;
		return format_[fileFormatId_[fileId]];
	}

	// genAudio後使用可能
	const std::vector<EncodeFileKey>& getOutFileKeys() const {
		return outFileKeys_;
	}

	// 映像データサイズ（バイト）、時間（タイムスタンプ）のペア
	std::pair<int64_t, double> getSrcVideoInfo(int videoFileIndex) const {
		return std::make_pair(filterSrcSize_[videoFileIndex], filterSrcDuration_[videoFileIndex]);
	}

	// TODO: VFR用タイムコード取得
	// infps: フィルタ入力のFPS
	// outpfs: フィルタ出力のFPS
	void getTimeCode(
		int encoderIndex, int videoFileIndex, CMType cmtype, double infps, double outfps) const
	{
		//
	}

	const std::vector<int64_t>& getAudioFileOffsets() const {
		return audioFileOffsets_;
	}

	bool isVFR() const {
		return isVFR_;
	}

	bool hasRFF() const {
		return hasRFF_;
	}

	double getInDuration() const {
		return srcTotalDuration_;
	}

	std::pair<double, double> getInOutDuration() const {
		return std::make_pair(srcTotalDuration_, outTotalDuration_);
	}

	void printOutputMapping(std::function<tstring(EncodeFileKey)> getFileName) const
	{
		ctx.info("[出力ファイル]");
		for (int i = 0; i < (int)outFileKeys_.size(); ++i) {
			ctx.infoF("%d: %s", i, getFileName(outFileKeys_[i]));
		}

		ctx.info("[入力->出力マッピング]");
		double fromPTS = dataPTS_[0];
		int prevFileId = 0;
		for (int i = 0; i < (int)ordredVideoFrame_.size(); ++i) {
			int ordered = ordredVideoFrame_[i];
			double pts = modifiedPTS_[ordered];
			int fileId = frameFormatId_[ordered];
			if (prevFileId != fileId) {
				// print
				auto from = elapsedTime(fromPTS);
				auto to = elapsedTime(pts);
				ctx.infoF("%3d分%05.3f秒 - %3d分%05.3f秒 -> %d",
					from.first, from.second, to.first, to.second, fileFormatId_[prevFileId]);
				prevFileId = fileId;
				fromPTS = pts;
			}
		}
		auto from = elapsedTime(fromPTS);
		auto to = elapsedTime(dataPTS_.back());
		ctx.infoF("%3d分%05.3f秒 - %3d分%05.3f秒 -> %d",
			from.first, from.second, to.first, to.second, fileFormatId_[prevFileId]);
	}

	// 以下デバッグ用 //

	void serialize(const tstring& path) {
		serialize(File(path, _T("wb")));
	}

	void serialize(const File& file) {
		file.writeValue(numVideoFile_);
		file.writeArray(videoFrameList_);
		file.writeArray(audioFrameList_);
		WriteArray(file, captionItemList_);
		file.writeArray(streamEventList_);
		file.writeArray(timeList_);
	}

	static StreamReformInfo deserialize(AMTContext& ctx, const tstring& path) {
		return deserialize(ctx, File(path, _T("rb")));
	}

	static StreamReformInfo deserialize(AMTContext& ctx, const File& file) {
		int numVideoFile = file.readValue<int>();
		auto videoFrameList = file.readArray<FileVideoFrameInfo>();
		auto audioFrameList = file.readArray<FileAudioFrameInfo>();
		auto captionList = ReadArray<CaptionItem>(file);
		auto streamEventList = file.readArray<StreamEvent>();
		auto timeList = file.readArray<TimeInfo>();
		return StreamReformInfo(ctx,
			numVideoFile, videoFrameList, audioFrameList, captionList, streamEventList, timeList);
	}

private:

	struct CaptionDuration {
		double startPTS, endPTS;
	};

	// 主要インデックスの説明
	// DTS順: 全映像フレームをDTS順で並べたときのインデックス
	// PTS順: 全映像フレームをPTS順で並べたときのインデックス
	// 中間映像ファイル順: 中間映像ファイルのインデックス(=video)
	// フォーマット順: 全フォーマットのインデックス
	// フォーマット(出力)順: 基本的にフォーマットと同じだが、「メイン以外は結合しない」場合、
	//                     メイン以外が分離されて異なるインデックスになっている(=format)
	// 出力ファイル順: EncodeFileKeyで識別される出力ファイルのインデックス

	// 入力解析の出力
	int numVideoFile_;
	std::vector<FileVideoFrameInfo> videoFrameList_; // [DTS順] 
	std::vector<FileAudioFrameInfo> audioFrameList_;
	std::vector<CaptionItem> captionItemList_;
	std::vector<StreamEvent> streamEventList_;
	std::vector<TimeInfo> timeList_;

	std::array<std::vector<NicoJKLine>, NICOJK_MAX> nicoJKList_;

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

	std::vector<OutVideoFormat> format_; // [フォーマット順]
	// 中間映像ファイルごとのフォーマット開始インデックス
	// サイズは中間映像ファイル数+1
	std::vector<int> formatStartIndex_; // [中間映像ファイル順]

	std::vector<int> fileFormatId_; // [フォーマット(出力)順] -> [フォーマット順] 変換
	// 中間映像ファイルごとのファイル開始インデックス
	// サイズは中間映像ファイル数+1
	std::vector<int> fileFormatStartIndex_; // [中間映像ファイル順] -> [フォーマット(出力)順]

	// 中間映像ファイルごと
	std::vector<std::vector<FilterSourceFrame>> filterFrameList_; // [PTS順]
	std::vector<std::vector<FilterAudioFrame>> filterAudioFrameList_;
	std::vector<int64_t> filterSrcSize_;
	std::vector<double> filterSrcDuration_;
	std::vector<std::vector<int>> fileDivs_; // CM解析結果

	std::vector<int> frameFormatId_; // [DTS順] -> [フォーマット(出力)順]

	// 出力ファイルリスト
	std::vector<EncodeFileKey> outFileKeys_; // [出力ファイル順]
	std::map<int, EncodeFileInput> outFiles_; // キーはEncodeFileKey.key()

	// 最初の映像フレームの時刻(UNIX時間)
	time_t firstFrameTime_;

	std::vector<int64_t> audioFileOffsets_; // 音声ファイルキャッシュ用

	double srcTotalDuration_;
	double outTotalDuration_;

	void reformMain(bool splitSub)
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

		// 各コンポーネント開始PTSを映像フレーム基準のラップアラウンドしないPTSに変換
		//（これをやらないと開始フレーム同士が間にラップアラウンドを挟んでると比較できなくなる）
		std::vector<int64_t> startPTSs;
		startPTSs.push_back(videoFrameList_[0].PTS);
		startPTSs.push_back(audioFrameList_[0].PTS);
		if (captionItemList_.size() > 0) {
			startPTSs.push_back(captionItemList_[0].PTS);
		}
		int64_t modifiedStartPTS[3];
		int64_t prevPTS = startPTSs[0];
		for (int i = 0; i < int(startPTSs.size()); ++i) {
			int64_t PTS = startPTSs[i];
			int64_t modPTS = prevPTS + int64_t((int32_t(PTS) - int32_t(prevPTS)));
			modifiedStartPTS[i] = modPTS;
			prevPTS = modPTS;
		}

		// 各コンポーネントのラップアラウンドしないPTSを生成
		makeModifiedPTS(modifiedStartPTS[0], modifiedPTS_, videoFrameList_);
		makeModifiedPTS(modifiedStartPTS[1], modifiedAudioPTS_, audioFrameList_);
		makeModifiedPTS(modifiedStartPTS[2], modifiedCaptionPTS_, captionItemList_);

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
		captionDuration_.resize(captionItemList_.size());
		double curEnd = dataPTS_.back();
		for (int i = (int)captionItemList_.size() - 1; i >= 0; --i) {
			double modPTS = modifiedCaptionPTS_[i] + (captionItemList_[i].waitTime * (MPEG_CLOCK_HZ / 1000));
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

		// 現在の音声フォーマットを保持
		// 音声ES数が変化しても前の音声フォーマットと変わらない場合は
		// イベントが飛んでこないので、現在の音声ES数とは関係なく全音声フォーマットを保持する
		std::vector<AudioFormat> curAudioFormats;

		OutVideoFormat curFormat = OutVideoFormat();
		double startPts = -1;
		double curFromPTS = -1;
		double curVideoFromPTS = -1;
		curFormat.videoFileId = -1;
		auto addSection = [&]() {
			registerOrGetFormat(curFormat);
			sectionFormatList.push_back(curFormat.formatId);
			startPtsList.push_back(curFromPTS);
			if (startPts == -1) {
				startPts = curFromPTS;
			}
			ctx.infoF("%.2f -> %d", (curFromPTS - startPts) / 90000.0, curFormat.formatId);
			curFromPTS = -1;
			curVideoFromPTS = -1;
		};
		for (int i = 0; i < (int)streamEventList_.size(); ++i) {
			auto& ev = streamEventList_[i];
			double pts = streamEventPTS_[i];
			if (pts >= endPTS) {
				// 後ろに映像がなければ意味がない
				continue;
			}
			if (curFromPTS != -1 &&          // fromがある
				curFormat.videoFileId >= 0 &&  // 映像がある
				curFromPTS + CHANGE_TORELANCE < pts) // CHANGE_TORELANCEより離れている
			{
				// 区間を追加
				addSection();
			}
			// 変更を反映
			switch (ev.type) {
			case PID_TABLE_CHANGED:
				if (curAudioFormats.size() < ev.numAudio) {
					curAudioFormats.resize(ev.numAudio);
				}
				if (curFormat.audioFormat.size() != ev.numAudio) {
					curFormat.audioFormat.resize(ev.numAudio);
					for (int i = 0; i < ev.numAudio; ++i) {
						curFormat.audioFormat[i] = curAudioFormats[i];
					}
					if (curFromPTS == -1) {
						curFromPTS = pts;
					}
				}
				break;
			case VIDEO_FORMAT_CHANGED:
				// ファイル変更
				if (!curFormat.videoFormat.isBasicEquals(videoFrameList_[ev.frameIdx].format)) {
					// アスペクト比以外も変更されていたらファイルを分ける
					//（AMTSplitterと条件を合わせなければならないことに注意）
					++curFormat.videoFileId;
					formatStartIndex_.push_back((int)format_.size());
				}
				curFormat.videoFormat = videoFrameList_[ev.frameIdx].format;
				if (curVideoFromPTS != -1) {
					// 映像フォーマットの変更を区間として取りこぼすと
					// AMTSplitterとの整合性が取れなくなるので強制的に追加
					addSection();
				}
				// 映像フォーマットの変更時刻を優先させる
				curFromPTS = curVideoFromPTS = dataPTS_[ev.frameIdx];
				break;
			case AUDIO_FORMAT_CHANGED:
				if (ev.audioIdx >= (int)curFormat.audioFormat.size()) {
					THROW(FormatException, "StreamEvent's audioIdx exceeds numAudio of the previous table change event");
				}
				curFormat.audioFormat[ev.audioIdx] = audioFrameList_[ev.frameIdx].format;
				curAudioFormats[ev.audioIdx] = audioFrameList_[ev.frameIdx].format;
				if (curFromPTS == -1) {
					curFromPTS = pts;
				}
				break;
			}
		}
		// 最後の区間を追加
		if (curFromPTS != -1) {
			addSection();
		}
		startPtsList.push_back(endPTS);
		formatStartIndex_.push_back((int)format_.size());

		// frameSectionIdを生成
		std::vector<int> outFormatFrames(format_.size());
		std::vector<int> frameSectionId(videoFrameList_.size());
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
			frameSectionId[i] = sectionId;
			outFormatFrames[sectionFormatList[sectionId]]++;
		}

		// セクション→ファイルマッピングを生成
		std::vector<int> sectionFileList(sectionFormatList.size());

		if (splitSub) {
			// メインフォーマット以外は結合しない //

			int mainFormatId = int(std::max_element(
				outFormatFrames.begin(), outFormatFrames.end()) - outFormatFrames.begin());

			fileFormatStartIndex_.push_back(0);
			for (int i = 0, mainFileId = -1, nextFileId = 0, videoId = 0;
				i < (int)sectionFormatList.size(); ++i)
			{
				int vid = format_[sectionFormatList[i]].videoFileId;
				if (videoId != vid) {
					fileFormatStartIndex_.push_back(nextFileId);
					videoId = vid;
				}
				if (sectionFormatList[i] == mainFormatId) {
					if (mainFileId == -1) {
						mainFileId = nextFileId++;
						fileFormatId_.push_back(mainFormatId);
					}
					sectionFileList[i] = mainFileId;
				}
				else {
					sectionFileList[i] = nextFileId++;
					fileFormatId_.push_back(sectionFormatList[i]);
				}
			}
			fileFormatStartIndex_.push_back((int)fileFormatId_.size());
		}
		else {
			for (int i = 0; i < (int)sectionFormatList.size(); ++i) {
				// ファイルとフォーマットは同じ
				sectionFileList[i] = sectionFormatList[i];
			}
			for (int i = 0; i < (int)format_.size(); ++i) {
				// ファイルとフォーマットは恒等変換
				fileFormatId_.push_back(i);
			}
			fileFormatStartIndex_ = formatStartIndex_;
		}

		// frameFormatId_を生成
		frameFormatId_.resize(videoFrameList_.size());
		for (int i = 0; i < int(videoFrameList_.size()); ++i) {
			frameFormatId_[i] = sectionFileList[frameSectionId[i]];
		}

		// フィルタ用入力フレームリスト生成
		filterFrameList_ = std::vector<std::vector<FilterSourceFrame>>(numVideoFile_);
		for (int videoId = 0; videoId < (int)numVideoFile_; ++videoId) {
			int keyFrame = -1;
			std::vector<FilterSourceFrame>& list = filterFrameList_[videoId];

			const auto& format = format_[formatStartIndex_[videoId]].videoFormat;
			double timePerFrame = format.frameRateDenom * MPEG_CLOCK_HZ / (double)format.frameRateNum;

			for (int i = 0; i < (int)videoFrameList_.size(); ++i) {
				int ordered = ordredVideoFrame_[i];
				int formatId = fileFormatId_[frameFormatId_[ordered]];
				if (format_[formatId].videoFileId == videoId) {

					double mPTS = modifiedPTS_[ordered];
					FileVideoFrameInfo& srcframe = videoFrameList_[ordered];
					if (srcframe.isGopStart) {
						keyFrame = int(list.size());
					}

					// まだキーフレームがない場合は捨てる
					if (keyFrame == -1) continue;

					FilterSourceFrame frame;
					frame.halfDelay = false;
					frame.frameIndex = i;
					frame.pts = mPTS;
					frame.frameDuration = timePerFrame; // TODO: VFR対応
					frame.framePTS = (int64_t)mPTS;
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
		for (int i = 0; i < (int)format_.size(); ++i) {
			numMaxAudio = std::max(numMaxAudio, (int)format_[i].audioFormat.size());
		}
		indexAudioFrameList_.resize(numMaxAudio);
		for (int i = 0; i < (int)audioFrameList_.size(); ++i) {
			// 短すぎてセクションとして認識されなかった部分に
			// numMaxAudioを超える音声データが存在する可能性がある
			// 音声数を超えている音声フレームは無視する
			if (audioFrameList_[i].audioIdx < numMaxAudio) {
				indexAudioFrameList_[audioFrameList_[i].audioIdx].push_back(i);
			}
		}

		// audioFileOffsets_を生成
		audioFileOffsets_.resize(audioFrameList_.size() + 1);
		for (int i = 0; i < (int)audioFrameList_.size(); ++i) {
			audioFileOffsets_[i] = audioFrameList_[i].fileOffset;
		}
		const auto& lastFrame = audioFrameList_.back();
		audioFileOffsets_.back() = lastFrame.fileOffset + lastFrame.codedDataSize;

		// 時間情報
		srcTotalDuration_ = dataPTS_.back() - dataPTS_.front();
		if (timeList_.size() > 0) {
			auto ti = timeList_[0];
			// ラップアラウンドしてる可能性があるので上位ビットは捨てて計算
			double diff = (double)(int32_t(ti.first / 300 - dataPTS_.front())) / MPEG_CLOCK_HZ;
			tm t = tm();
			ti.second.getDay(t.tm_year, t.tm_mon, t.tm_mday);
			ti.second.getTime(t.tm_hour, t.tm_min, t.tm_sec);
			// 調整
			t.tm_mon -= 1; // 月は0始まりなので
			t.tm_year -= 1900; // 年は1900を引く
			t.tm_hour -= 9; // 日本なのでGMT+9
			t.tm_sec -= (int)std::round(diff); // 最初のフレームまで戻す
			firstFrameTime_ = _mkgmtime(&t);
		}

    fileDivs_.resize(numVideoFile_);
	}

	void calcSizeAndTime(const std::vector<CMType>& cmtypes)
	{
		// CM解析がないときはfileDivs_が設定されていないのでここで設定
		for (int i = 0; i < numVideoFile_; ++i) {
			auto& divs = fileDivs_[i];
			if (divs.size() == 0) {
				divs.push_back(0);
				divs.push_back((int)filterFrameList_[i].size());
			}
		}

		// ファイルリスト生成
		outFileKeys_.clear();
		for (int video = 0; video < numVideoFile_; ++video) {
			int numEncoders = getNumEncoders(video);
			for (int format = 0; format < numEncoders; ++format) {
				for (int div = 0; div < fileDivs_[video].size() - 1; ++div) {
					for (CMType cmtype : cmtypes) {
						outFileKeys_.push_back(EncodeFileKey(video, format, div, cmtype));
					}
				}
			}
		}

		// 各中間ファイルの入力ファイル時間とサイズを計算
		filterSrcSize_ = std::vector<int64_t>(numVideoFile_, 0);
		filterSrcDuration_ = std::vector<double>(numVideoFile_, 0);
		std::vector<double> fileFormatDuration(fileFormatId_.size(), 0);
		for (int i = 0; i < (int)videoFrameList_.size(); ++i) {
			int ordered = ordredVideoFrame_[i];
			const auto& frame = videoFrameList_[ordered];
			int fileFormatId = frameFormatId_[ordered];
			int formatId = fileFormatId_[fileFormatId];
			int videoId = format_[formatId].videoFileId;
			int next = (i + 1 < (int)videoFrameList_.size())
				? ordredVideoFrame_[i + 1]
				: -1;
			double duration = getSourceFrameDuration(ordered, next);
			// 中間ファイルごのサイズと時間（ソースビットレート計算用）
			filterSrcSize_[videoId] += frame.codedDataSize;
			filterSrcDuration_[videoId] += duration;
			// フォーマット（出力）ごとの時間（出力ファイル名決定用）
			fileFormatDuration[fileFormatId] += duration;
		}

		int maxId = (int)(std::max_element(fileFormatDuration.begin(), fileFormatDuration.end()) -
			fileFormatDuration.begin());

		// [フォーマット(出力)] -> [出力用番号] 作成
		// 最も時間の長いフォーマット(出力)がゼロ。それ以外は順番通り
		std::vector<int> formatOutIndex(fileFormatId_.size());
		formatOutIndex[maxId] = 0;
		for (int i = 0, cnt = 1; i < (int)formatOutIndex.size(); ++i) {
			if (i != maxId) {
				formatOutIndex[i] = cnt++;
			}
		}

		// 各出力ファイルのメタデータを作成
		for (auto key : outFileKeys_) {
			auto& file = outFiles_[key.key()];
			// [フォーマット(出力)順]
			int foramtId = fileFormatStartIndex_[key.video] + key.format;

			file.outKey.video = 0; // 使わない
			file.outKey.format = formatOutIndex[foramtId];
			file.outKey.div = key.div;
			file.outKey.cm = (key.cm == cmtypes[0]) ? CMTYPE_BOTH : key.cm;
			file.keyMax.video = 0; // 使わない
			file.keyMax.format = (int)fileFormatId_.size();
			file.keyMax.div = (int)fileDivs_[key.video].size() - 1;
			file.keyMax.cm = key.cm; // 使わない

			// フレームリスト作成
			file.videoFrames.clear();
			const auto& frameList = filterFrameList_[key.video];
			int start = fileDivs_[key.video][key.div];
			int end = fileDivs_[key.video][key.div + 1];
			for (int i = start; i < end; ++i) {
				if (foramtId == frameFormatId_[frameList[i].frameIndex]) {
					if (key.cm == CMTYPE_BOTH || key.cm == frameList[i].cmType) {
						file.videoFrames.push_back(i);
					}
				}
			}

			// 時間を計算
			file.duration = 0;
			for (int i = 0; i < (int)file.videoFrames.size(); ++i) {
				file.duration += frameList[file.videoFrames[i]].frameDuration;
			}
		}

		// 総出力時間
		outTotalDuration_ = 0;
		for (auto key : outFileKeys_) {
			outTotalDuration_ += outFiles_.at(key.key()).duration;
		}
	}

	template<typename I>
	void makeModifiedPTS(int64_t modifiedFirstPTS, std::vector<double>& modifiedPTS, const std::vector<I>& frames)
	{
		// 前後のフレームのPTSに6時間以上のずれがあると正しく処理できない
		if (frames.size() == 0) return;

		// ラップアラウンドしないPTSを生成
		modifiedPTS.resize(frames.size());
		int64_t prevPTS = modifiedFirstPTS;
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
				ctx.incrementCounter(AMT_ERR_NON_CONTINUOUS_PTS);
				ctx.warnF("PTSが戻っています。正しく処理できないかもしれません。 [%d] %.0f -> %.0f",
					i, modifiedPTS[i - 1], modifiedPTS[i]);
			}
		}
	}

	void registerOrGetFormat(OutVideoFormat& format) {
		// すでにあるのから探す
		for (int i = formatStartIndex_.back(); i < (int)format_.size(); ++i) {
			if (isEquealFormat(format_[i], format)) {
				format.formatId = i;
				return;
			}
		}
		// ないので登録
		format.formatId = (int)format_.size();
		format_.push_back(format);
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

	struct AudioState {
		double time = 0; // 追加された音声フレームの合計時間
		double lostPts = -1; // 同期ポイントを見失ったPTS（表示用）
		int lastFrame = -1;
	};

	struct OutFileState {
		int formatId; // デバッグ出力用
		double time; // 追加された映像フレームの合計時間
		std::vector<AudioState> audioState;
		FileAudioFrameList audioFrameList;
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
		// 各ファイルの音声構築
		for (int v = 0; v < (int)outFileKeys_.size(); ++v) {
			auto key = outFileKeys_[v];
			int formatId = fileFormatStartIndex_[key.video] + key.format;
			auto& file = outFiles_[key.key()];
			const auto& srcFrames = filterFrameList_[key.video];
			const auto& audioFormats = format_[fileFormatId_[formatId]].audioFormat;
			int numAudio = (int)audioFormats.size();
			OutFileState state;
			state.formatId = formatId;
			state.time = 0;
			state.audioState.resize(numAudio);
			state.audioFrameList.resize(numAudio);
			for (int i = 0; i < (int)file.videoFrames.size(); ++i) {
				const auto& frame = srcFrames[file.videoFrames[i]];
				addVideoFrame(state, audioFormats, frame.pts, frame.frameDuration, nullptr);
			}
			file.audioFrames = std::move(state.audioFrameList);
		}

		// 音ズレ統計情報のためもう1パス実行
		AudioDiffInfo adiff = initAudioDiffInfo();
		std::vector<OutFileState> states(fileFormatId_.size());
		for (int i = 0; i < (int)states.size(); ++i) {
			int numAudio = (int)format_[fileFormatId_[i]].audioFormat.size();
			states[i].formatId = i;
			states[i].time = 0;
			states[i].audioState.resize(numAudio);
			states[i].audioFrameList.resize(numAudio);
		}
		for (int videoId = 0; videoId < numVideoFile_; ++videoId) {
			const auto& frameList = filterFrameList_[videoId];
			for (int i = 0; i < (int)frameList.size(); ++i) {
				const auto& frame = frameList[i];
				int fileFormatId = frameFormatId_[frame.frameIndex];
				const auto& audioFormats = format_[fileFormatId_[fileFormatId]].audioFormat;
				addVideoFrame(states[fileFormatId],
					audioFormats, frame.pts, frame.frameDuration, &adiff);
			}
		}

		return adiff;
	}

	void genWaveAudioStream()
	{
		// 全映像フレームを追加
		ctx.info("[CM判定用音声構築]");
		filterAudioFrameList_.resize(numVideoFile_);
		for (int videoId = 0; videoId < (int)numVideoFile_; ++videoId) {
			OutFileState file = { 0 };
			file.formatId = -1;
			file.time = 0;
			file.audioState.resize(1);
			file.audioFrameList.resize(1);

			auto& frames = filterFrameList_[videoId];
			auto& format = format_[formatStartIndex_[videoId]];

			// AviSynthがVFRに対応していないので、CFR前提で問題ない
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
					fillAudioFrames(file, 0, nullptr, audioPts, audioDuration, nullptr);
				}
			}

			auto& list = file.audioFrameList[0];
			for (int i = 0; i < (int)list.size(); ++i) {
				FilterAudioFrame frame = { 0 };
				auto& info = audioFrameList_[list[i]];
				frame.frameIndex = list[i];
				frame.waveOffset = info.waveOffset;
				frame.waveLength = info.waveDataSize;
				filterAudioFrameList_[videoId].push_back(frame);
			}
		}
	}

	// ソースフレームの表示時間
	// index, nextIndex: DTS順
	double getSourceFrameDuration(int index, int nextIndex) {
		const auto& videoFrame = videoFrameList_[index];
		int formatId = fileFormatId_[frameFormatId_[index]];
		const auto& format = format_[formatId];
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
			switch (videoFrame.pic) {
			case PIC_FRAME:
			case PIC_TFF:
			case PIC_BFF:
				duration = frameDiff;
				break;
			case PIC_TFF_RFF:
			case PIC_BFF_RFF:
				duration = frameDiff * 1.5;
				hasRFF_ = true;
				break;
			case PIC_FRAME_DOUBLING:
				duration = frameDiff * 2;
				hasRFF_ = true;
				break;
			case PIC_FRAME_TRIPLING:
				duration = frameDiff * 3;
				hasRFF_ = true;
				break;
			}
		}

		return duration;
	}

	void addVideoFrame(OutFileState& file, 
		const std::vector<AudioFormat>& audioFormat,
		double pts, double duration, AudioDiffInfo* adiff)
	{
		double endPts = pts + duration;
		file.time += duration;

		ASSERT(audioFormat.size() == file.audioFrameList.size());
		ASSERT(audioFormat.size() == file.audioState.size());
		for (int i = 0; i < (int)audioFormat.size(); ++i) {
			// file.timeまで音声を進める
			auto& audioState = file.audioState[i];
			if (audioState.time >= file.time) {
				// 音声は十分進んでる
				continue;
			}
			double audioDuration = file.time - audioState.time;
			double audioPts = endPts - audioDuration;
			fillAudioFrames(file, i, &audioFormat[i], audioPts, audioDuration, adiff);
		}
	}

	void fillAudioFrames(
		OutFileState& file, int index, // 対象ファイルと音声インデックス
		const AudioFormat* format, // 音声フォーマット
		double pts, double duration, // 開始修正PTSと90kHzでのタイムスパン
		AudioDiffInfo* adiff)
	{
		auto& state = file.audioState[index];
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
					auto elapsed = elapsedTime(pts);
					ctx.debugF("%d分%.3f秒で音声%d-%dの同期ポイントを見失ったので再検索",
						elapsed.first, elapsed.second, file.formatId, index);
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
		auto& outFrameList = file.audioFrameList.at(index);
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
					auto elapsed = elapsedTime(modPTS);
					ctx.debugF("%d分%.3f秒で音声%d-%dにずれがあるので%dフレーム水増し",
						elapsed.first, elapsed.second, file.formatId, index, nframes - 1);
				}
				if (nskipped > 0) {
					if (state.lastFrame == -1) {
						ctx.debugF("音声%d-%dは%dフレーム目から開始",
							file.formatId, index, nskipped);
					}
					else {
						auto elapsed = elapsedTime(modPTS);
						ctx.debugF("%d分%.3f秒で音声%d-%dにずれがあるので%dフレームスキップ",
							elapsed.first, elapsed.second, file.formatId, index, nskipped);
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

	// ファイル全体での時間
	std::pair<int, double> elapsedTime(double modPTS) const {
		double sec = (double)(modPTS - dataPTS_[0]) / MPEG_CLOCK_HZ;
		int minutes = (int)(sec / 60);
		sec -= minutes * 60;
		return std::make_pair(minutes, sec);
	}

	void genCaptionStream()
	{
		ctx.info("[字幕構築]");

		for (int v = 0; v < (int)outFileKeys_.size(); ++v) {
			auto key = outFileKeys_[v];
			auto& file = outFiles_[key.key()];
			const auto& srcFrames = filterFrameList_[key.video];
			const auto& frames = file.videoFrames;
			std::vector<double> frameTimes;

			auto pred = [&](const int& f, double mid) { return srcFrames[f].pts < mid; };

			auto getFrameIndex = [&](double pts) {
				return std::lower_bound(
					frames.begin(), frames.end(), pts, pred) - frames.begin();
			};

			auto containsPTS = [&](double pts) {
				auto it = std::lower_bound(srcFrames.begin(), srcFrames.end(), pts,
					[](const FilterSourceFrame& frame, double mid) { return frame.pts < mid; });
				if (it != srcFrames.end()) {
					int idx = (int)(it - srcFrames.begin());
					auto it2 = std::lower_bound(frames.begin(), frames.end(), idx);
					if (it2 != frames.end() && *it2 == idx) {
						return true;
					}
				}
				return false;
			};

			double curTime = 0.0;
			for (int i = 0; i < (int)frames.size(); ++i) {
				frameTimes.push_back(curTime);
				curTime += srcFrames[frames[i]].frameDuration;
			}
			// 最終フレームの終了時刻も追加
			frameTimes.push_back(curTime);

			// 字幕を生成
			for (int i = 0; i < (int)captionItemList_.size(); ++i) {
				if (captionItemList_[i].line) { // クリア以外
					auto duration = captionDuration_[i];
					auto start = getFrameIndex(duration.startPTS);
					auto end = getFrameIndex(duration.endPTS);
					if (start < end) { // 1フレーム以上表示時間のある場合のみ
						int langIndex = captionItemList_[i].langIndex;
						if (langIndex >= file.captionList.size()) { // 言語が足りない場合は広げる
							file.captionList.resize(langIndex + 1);
						}
						OutCaptionLine outcap = {
							frameTimes[start], frameTimes[end], captionItemList_[i].line.get()
						};
						file.captionList[langIndex].push_back(outcap);
					}
				}
			}

			// ニコニコ実況コメントを生成
			for (int t = 0; t < NICOJK_MAX; ++t) {
				auto& srcList = nicoJKList_[t];
				for (int i = 0; i < (int)srcList.size(); ++i) {
					auto item = srcList[i];
					// 開始がこのファイルに含まれているか
					if (containsPTS(item.start)) {
						double startTime = frameTimes[getFrameIndex(item.start)];
						double endTime = frameTimes[getFrameIndex(item.end)];
						NicoJKLine outcomment = { startTime, endTime, item.line };
						file.nicojkList[t].push_back(outcomment);
					}
				}
			}
		}
	}
};

