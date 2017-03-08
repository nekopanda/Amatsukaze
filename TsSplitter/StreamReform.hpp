#pragma once

#include <vector>
#include <map>
#include <memory>

#include "StreamUtils.hpp"

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

class StreamReformInfo : public AMTObject {
public:
	StreamReformInfo(
		AMTContext& ctx,
		int numVideoFile,
		std::vector<VideoFrameInfo>& videoFrameList,
		std::vector<FileAudioFrameInfo>& audioFrameList,
		std::vector<StreamEvent>& streamEventList)
		: AMTObject(ctx)
		, numVideoFile_(numVideoFile)
		, videoFrameList_(std::move(videoFrameList))
		, audioFrameList_(std::move(audioFrameList))
		, streamEventList_(std::move(streamEventList))
	{
		encodedFrames_.resize(videoFrameList_.size(), false);
	}

	void prepareEncode() {
		reformMain();
	}

	void prepareMux() {
		genAudioStream();
	}

	int getNumVideoFile() const {
		return numVideoFile_;
	}

	// PTS -> video frame index
	int getVideoFrameIndex(int64_t PTS, int videoFileIndex) const {
		auto it = framePtsMap_.find(PTS);
		if (it == framePtsMap_.end()) {
			return -1;
		}

		int frameIndex = it->second;

		// check videoFileIndex
		int formatId = frameFormatId_[frameIndex];
		int start = outFormatStartIndex_[videoFileIndex];
		int end = outFormatStartIndex_[videoFileIndex + 1];
		if (formatId < start || formatId >= end) {
			return -1;
		}

		return frameIndex;
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

	const FileAudioFrameList& getFileAudioFrameList(
		int encoderIndex, int videoFileIndex) const
	{
		int formatId = outFormatStartIndex_[videoFileIndex] + encoderIndex;
		return *reformedAudioFrameList_[formatId].get();
	}

	// 各ファイルの再生時間
	int64_t getFileDuration(int encoderIndex, int videoFileIndex) const {
		int formatId = outFormatStartIndex_[videoFileIndex] + encoderIndex;
		return fileDuration_[formatId];
	}

	const std::vector<int64_t>& getAudioFileOffsets() const {
		return audioFileOffsets_;
	}

	int getOutFileIndex(int encoderIndex, int videoFileIndex) const {
		int formatId = outFormatStartIndex_[videoFileIndex] + encoderIndex;
		return outFileIndex_[formatId];
	}

private:
	// 1st phase 出力
	int numVideoFile_;
	std::vector<VideoFrameInfo> videoFrameList_;
	std::vector<FileAudioFrameInfo> audioFrameList_;
	std::vector<StreamEvent> streamEventList_;

	// 計算データ
	std::vector<int64_t> modifiedPTS_; // ラップアラウンドしないPTS
	std::vector<int64_t> modifiedAudioPTS_; // ラップアラウンドしないPTS
	std::vector<int> audioFrameDuration_; // 各音声フレームの時間
	std::vector<int> ordredVideoFrame_;
	std::vector<int64_t> dataPTS_; // 映像フレームのストリーム上での位置とPTSの関連付け
	std::vector<int64_t> streamEventPTS_;

	std::vector<std::vector<int>> indexAudioFrameList_; // 音声インデックスごとのフレームリスト

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
	std::vector<int64_t> fileDuration_;
	std::vector<int64_t> audioFileOffsets_; // 音声ファイルキャッシュ用
	std::vector<int> outFileIndex_;

	// 音ズレ情報
	int64_t sumPtsDiff_;
	int totalAudioFrames_; // 出力した音声フレーム（水増し分を含む）
	int totalUniquAudioFrames_; // 出力した音声フレーム（水増し分を含まず）
	int64_t maxPtsDiff_;
	int64_t maxPtsDiffPos_;

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
		makeModifiedPTS(modifiedPTS_, videoFrameList_);
		makeModifiedPTS(modifiedAudioPTS_, audioFrameList_);

		// audioFrameDuration_を生成
		audioFrameDuration_.resize(audioFrameList_.size());
		for (int i = 0; i < (int)audioFrameList_.size(); ++i) {
			const auto& frame = audioFrameList_[i];
			audioFrameDuration_[i] = int((frame.numSamples * MPEG_CLOCK_HZ) / frame.format.sampleRate);
		}

		// ptsOrdredVideoFrame_を生成
		ordredVideoFrame_.resize(videoFrameList_.size());
		for (int i = 0; i < (int)videoFrameList_.size(); ++i) {
			ordredVideoFrame_[i] = i;
		}
		std::sort(ordredVideoFrame_.begin(), ordredVideoFrame_.end(), [&](int a, int b) {
			return videoFrameList_[a].PTS < videoFrameList_[b].PTS;
		});

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
				outFormatStartIndex_.push_back((int)outFormat_.size());
				curFormat.videoFormat = videoFrameList_[ev.frameIdx].format;
				break;
			case AUDIO_FORMAT_CHANGED:
				if (ev.audioIdx >= curFormat.audioFormat.size()) {
					THROW(FormatException, "StreamEvent's audioIdx exceeds numAudio of the previous table change event");
				}
				curFormat.audioFormat[ev.audioIdx] = audioFrameList_[ev.frameIdx].format;
				break;
			}
		}
		// 最後の区間を追加
		curSection.toPTS = exceedLastPTS;
		registerOrGetFormat(curFormat);
		curSection.formatId = curFormat.formatId;
		sectionList.push_back(curSection);
		outFormatStartIndex_.push_back((int)outFormat_.size());

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

	template<typename I>
	static void makeModifiedPTS(std::vector<int64_t>& modifiedPTS, const std::vector<I>& frames)
	{
		// 前後のフレームのPTSに6時間以上のずれがあると正しく処理できない

		// ラップアラウンドしないPTSを生成
		modifiedPTS.resize(frames.size());
		int64_t prevPTS = frames[0].PTS;
		for (int i = 0; i < int(frames.size()); ++i) {
			int64_t PTS = frames[i].PTS;
			int64_t modPTS = prevPTS + int64_t((int32_t(PTS) - int32_t(prevPTS)));
			modifiedPTS[i] = modPTS;
			prevPTS = PTS;
		}

		// ストリームが戻っている場合は処理できないのでエラーとする
		for (int i = 1; i < int(frames.size()); ++i) {
			if (modifiedPTS[i] - modifiedPTS[i - 1] < -60 * MPEG_CLOCK_HZ) {
				// 1分以上戻っていたらエラーとする
				THROWF(FormatException,
					"PTSが戻っています。処理できません。 %llu -> %llu",
					modifiedPTS[i - 1], modifiedPTS[i]);
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

	static int numGenerateFrames(const VideoFrameInfo& frameInfo) {
		// BFF RFFだけ2枚、他は1枚
		return (frameInfo.pic == PIC_BFF_RFF) ? 2 : 1;
	}

	struct AudioState {
		int64_t time = 0; // 追加された音声フレームの合計時間
		int lastFrame = -1;
	};

	struct OutFileState {
		int64_t time; // 追加された映像フレームの合計時間
		std::vector<AudioState> audioState;
		std::unique_ptr<FileAudioFrameList> audioFrameList;
	};

	void genAudioStream() {

		// 統計情報初期化
		sumPtsDiff_ = 0;
		totalAudioFrames_ = 0;
		totalUniquAudioFrames_ = 0;
		maxPtsDiff_ = 0;
		maxPtsDiffPos_ = 0;

		// indexAudioFrameList_を作成
		int numMaxAudio = 1;
		for (int i = 0; i < (int)outFormat_.size(); ++i) {
			numMaxAudio = std::max(numMaxAudio, (int)outFormat_[i].audioFormat.size());
		}
		indexAudioFrameList_.resize(numMaxAudio);
		for (int i = 0; i < (int)audioFrameList_.size(); ++i) {
			indexAudioFrameList_[audioFrameList_[i].audioIdx].push_back(i);
		}

		std::vector<OutFileState> outFiles(outFormat_.size());

		// outFiles初期化
		for (int i = 0; i < (int)outFormat_.size(); ++i) {
			auto& file = outFiles[i];
			int numAudio = (int)outFormat_[i].audioFormat.size();
			file.time = 0;
			file.audioState.resize(numAudio);
			file.audioFrameList =
				std::unique_ptr<FileAudioFrameList>(new FileAudioFrameList(numAudio));
		}

		// 全映像フレームを追加
		for (int i = 0; i < (int)videoFrameList_.size(); ++i) {
			// 映像フレームはPTS順に追加する
			int ordered = ordredVideoFrame_[i];
			if (encodedFrames_[ordered]) {
				int formatId = frameFormatId_[ordered];
				addVideoFrame(outFiles[formatId], ordered);
			}
		}

		// 出力データ生成
		reformedAudioFrameList_.resize(outFormat_.size());
		fileDuration_.resize(outFormat_.size());
		int64_t maxDuration = 0;
		int maxId = 0;
		for (int i = 0; i < (int)outFormat_.size(); ++i) {
			int64_t time = outFiles[i].time;
			reformedAudioFrameList_[i] = std::move(outFiles[i].audioFrameList);
			fileDuration_[i] = time;
			if (maxDuration < time) {
				maxDuration = time;
				maxId = i;
			}
		}

		// audioFileOffsets_を生成
		audioFileOffsets_.resize(audioFrameList_.size() + 1);
		for (int i = 0; i < (int)audioFrameList_.size(); ++i) {
			audioFileOffsets_[i] = audioFrameList_[i].fileOffset;
		}
		const auto& lastFrame = audioFrameList_.back();
		audioFileOffsets_.back() = lastFrame.fileOffset + lastFrame.codedDataSize;

		// 出力ファイル番号生成
		outFileIndex_.resize(outFormat_.size());
		outFileIndex_[maxId] = 0;
		for (int i = 0, cnt = 1; i < (int)outFormat_.size(); ++i) {
			if (i != maxId) {
				outFileIndex_[i] = cnt++;
			}
		}

		printAudioPtsDiff();
	}

	// ファイルに映像フレームを１枚追加
	void addVideoFrame(OutFileState& file, int index) {
		const auto& videoFrame = videoFrameList_[index];
		int64_t pts = modifiedPTS_[index];
		int formatId = frameFormatId_[index];
		const auto& format = outFormat_[formatId];
		int64_t duration = format.videoFormat.frameRateDenom * MPEG_CLOCK_HZ / format.videoFormat.frameRateNum;

		switch (videoFrame.pic) {
		case PIC_FRAME:
		case PIC_TFF:
		case PIC_TFF_RFF:
			break;
		case PIC_FRAME_DOUBLING:
			duration *= 2;
			break;
		case PIC_FRAME_TRIPLING:
			duration *= 3;
			break;
		case PIC_BFF:
			pts -= (duration / 2);
			break;
		case PIC_BFF_RFF:
			pts -= (duration / 2);
			duration *= 2;
			break;
		}

		int64_t endPts = pts + duration;
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
			int64_t audioDuration = file.time - audioState.time;
			int64_t audioPts = endPts - audioDuration;
			fillAudioFrames(file, i, format.audioFormat[i], audioPts, audioDuration);
		}
	}

	void fillAudioFrames(
		OutFileState& file, int index, // 対象ファイルと音声インデックス
		const AudioFormat& format, // 音声フォーマット
		int64_t pts, int64_t duration) // 開始修正PTSと90kHzでのタイムスパン
	{
		auto& state = file.audioState[index];
		auto& outFrameList = file.audioFrameList->at(index);
		const auto& frameList = indexAudioFrameList_[index];

		fillAudioFramesInOrder(file, index, format, pts, duration);
		if (duration <= 0) {
			// 十分出力した
			return;
		}

		// もしかしたら戻ったらあるかもしれないので探しなおす
		auto it = std::partition_point(frameList.begin(), frameList.end(), [&](int frameIndex) {
			int64_t modPTS = modifiedAudioPTS_[frameIndex];
			int frameDuration = audioFrameDuration_[frameIndex];
			return modPTS + (frameDuration / 2) < pts;
		});
		if (it != frameList.end()) {
			// 見つけたところに位置をセットして入れてみる
			ctx.warn("%f秒で音声%dの同期ポイントを見失ったので再検索",
				elapsedTime(pts), index);
			state.lastFrame = *it - 1;
			fillAudioFramesInOrder(file, index, format, pts, duration);
		}

		// 有効な音声フレームが見つからなかった場合はとりあえず何もしない
		// 次に有効な音声フレームが見つかったらその間はフレーム水増しされる
		// 映像より音声が短くなる可能性はあるが、有効な音声がないのであれば仕方ないし
		// 音ズレするわけではないので問題ないと思われる

	}

	// lastFrameから順番に見て音声フレームを入れる
	void fillAudioFramesInOrder(
		OutFileState& file, int index, // 対象ファイルと音声インデックス
		const AudioFormat& format, // 音声フォーマット
		int64_t& pts, int64_t& duration) // 開始修正PTSと90kHzでのタイムスパン
	{
		auto& state = file.audioState[index];
		auto& outFrameList = file.audioFrameList->at(index);
		const auto& frameList = indexAudioFrameList_[index];
		int nskipped = 0;

		for (int i = state.lastFrame + 1; i < (int)frameList.size(); ++i) {
			int frameIndex = frameList[i];
			const auto& frame = audioFrameList_[frameIndex];
			int64_t modPTS = modifiedAudioPTS_[frameIndex];
			int frameDuration = audioFrameDuration_[frameIndex];

			if (modPTS >= pts + duration) {
				// 行き過ぎ
				break;
			}
			if (modPTS + (frameDuration / 2) < pts) {
				// 前すぎるのでスキップ
				++nskipped;
				continue;
			}
			if (frame.format != format) {
				// フォーマットが違うのでスキップ
				continue;
			}

			// 空きがある場合はフレームを水増しする
			// フレームの4分の3以上の空きができる場合は埋める
			int nframes = std::max(1, ((int)(modPTS - pts) + (frameDuration / 4)) / frameDuration);

			if (nframes > 1) {
				ctx.warn("%f秒で音声%dにずれがあるので%dフレーム水増しします。",
					elapsedTime(modPTS), index, nframes - 1);
			}
			if (nskipped > 0) {
				if (state.lastFrame == -1) {
					ctx.info("音声%dは%dフレーム目から開始します。",
						index, nskipped);
				}
				else {
					ctx.warn("%f秒で音声%dにずれがあるので%dフレームスキップします。",
						elapsedTime(modPTS), index, nskipped);
				}
				nskipped = 0;
			}

			++totalUniquAudioFrames_;
			for (int t = 0; t < nframes; ++t) {
				// 統計情報
				int64_t diff = std::abs(modPTS - pts);
				if (maxPtsDiff_ < diff) {
					maxPtsDiff_ = diff;
					maxPtsDiffPos_ = pts;
				}
				sumPtsDiff_ += diff;
				++totalAudioFrames_;

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

	void printAudioPtsDiff() {
		double avgDiff = (double)sumPtsDiff_ * 1000 / (totalAudioFrames_ * MPEG_CLOCK_HZ);
		double maxDiff = (double)maxPtsDiff_ * 1000 / MPEG_CLOCK_HZ;

		ctx.info("音声フレーム %d（うち水増し%dフレーム）出力しました。",
			totalAudioFrames_, totalAudioFrames_ - totalUniquAudioFrames_);
		ctx.info("音ズレは 平均 %.2fms 最大 %.2fms です。",
			avgDiff, maxDiff);

		if (maxPtsDiff_ > 0 && maxDiff - avgDiff > 1) {
			ctx.info("最大音ズレ位置: 入力動画最初の映像フレームから%f秒後",
				elapsedTime(maxPtsDiffPos_));
		}
	}

	double elapsedTime(int64_t modPTS) {
		return (double)(modPTS - dataPTS_[0]) / MPEG_CLOCK_HZ;
	}
};

