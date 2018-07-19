/**
* Call muxer
* Copyright (c) 2017-2018 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include "StreamUtils.hpp"
#include "TranscodeSetting.hpp"
#include "StreamReform.hpp"
#include "PacketCache.hpp"
#include "EncoderOptionParser.hpp"
#include "AdtsParser.hpp"
#include "ProcessThread.hpp"

struct EncodeFileInfo {
	std::string outPath;
	std::string tcPath; // タイムコードファイルパス
	std::vector<std::string> outSubs; // 外部ファイルで出力された字幕
	int64_t fileSize;
	double srcBitrate;
	double targetBitrate;
};

class AMTMuxder : public AMTObject {
public:
	AMTMuxder(
		AMTContext&ctx,
		const ConfigWrapper& setting,
		const StreamReformInfo& reformInfo)
		: AMTObject(ctx)
		, setting_(setting)
		, reformInfo_(reformInfo)
		, audioCache_(ctx, setting.getAudioFilePath(), reformInfo.getAudioFileOffsets(), 12, 4)
	{ }

	void mux(int videoFileIndex, int encoderIndex, CMType cmtype,
		const VideoFormat& fvfmt, // フィルタ出力フォーマット
		const EncoderOptionInfo& eoInfo, // エンコーダオプション情報
		const OutPathGenerator& pathgen, // パス生成器
		bool nicoOK,
		EncodeFileInfo& outFilePath) // 出力情報
	{
		auto fmt = reformInfo_.getFormat(encoderIndex, videoFileIndex);
		auto vfmt = fvfmt;

		if (vfmt.progressive == false) {
			// エンコーダでインタレ解除している場合があるので、それを反映する
			switch (eoInfo.deint) {
			case ENCODER_DEINT_24P:
				vfmt.mulDivFps(4, 5);
				vfmt.progressive = true;
				break;
			case ENCODER_DEINT_30P:
			case ENCODER_DEINT_VFR:
				vfmt.progressive = true;
				break;
			case ENCODER_DEINT_60P:
				vfmt.mulDivFps(2, 1);
				vfmt.progressive = true;
				break;
			}
		}
		else {
			if (eoInfo.deint != ENCODER_DEINT_NONE) {
				// 一応警告を出す
				ctx.warn("エンコーダへの入力はプログレッシブですが、"
					"エンコーダオプションでインタレ解除指定がされています。");
				ctx.warn("エンコーダでこのオプションが無視される場合は問題ありません。");
			}
		}

		// 音声ファイルを作成
		std::vector<std::string> audioFiles;
		const FileAudioFrameList& fileFrameList =
			reformInfo_.getFileAudioFrameList(encoderIndex, videoFileIndex, cmtype);
		for (int asrc = 0, adst = 0; asrc < (int)fileFrameList.size(); ++asrc) {
			const std::vector<int>& frameList = fileFrameList[asrc];
			if (frameList.size() > 0) {
				if (fmt.audioFormat[asrc].channels == AUDIO_2LANG) {
					// デュアルモノは2つのAACに分離
					ctx.info("音声%d-%dはデュアルモノなので2つのAACファイルに分離します", encoderIndex, asrc);
					SpDualMonoSplitter splitter(ctx);
					std::string filepath0 = setting_.getIntAudioFilePath(videoFileIndex, encoderIndex, adst++, cmtype);
					std::string filepath1 = setting_.getIntAudioFilePath(videoFileIndex, encoderIndex, adst++, cmtype);
					splitter.open(0, filepath0);
					splitter.open(1, filepath1);
					for (int frameIndex : frameList) {
						splitter.inputPacket(audioCache_[frameIndex]);
					}
					audioFiles.push_back(filepath0);
					audioFiles.push_back(filepath1);
				}
				else {
					std::string filepath = setting_.getIntAudioFilePath(videoFileIndex, encoderIndex, adst++, cmtype);
					File file(filepath, "wb");
					for (int frameIndex : frameList) {
						file.write(audioCache_[frameIndex]);
					}
					audioFiles.push_back(filepath);
				}
			}
		}

		// 映像ファイル
		std::string encVideoFile;
		encVideoFile = setting_.getEncVideoFilePath(videoFileIndex, encoderIndex, cmtype);

		// チャプターファイル
		std::string chapterFile;
		if (setting_.isChapterEnabled()) {
			auto path = setting_.getTmpChapterPath(videoFileIndex, encoderIndex, cmtype);
			if (File::exists(path)) {
				chapterFile = path;
			}
		}

		// 字幕ファイル
		std::vector<std::string> subsFiles;
		std::vector<std::string> subsTitles;
		if (nicoOK) {
			for (NicoJKType jktype : setting_.getNicoJKTypes()) {
				auto srcsub = setting_.getTmpNicoJKASSPath(videoFileIndex, encoderIndex, cmtype, jktype);
				if (setting_.getFormat() == FORMAT_MKV) {
					subsFiles.push_back(srcsub);
					subsTitles.push_back(StringFormat("NicoJK%s", GetNicoJKSuffix(jktype)));
				}
				else { // MP4の場合は別ファイルとしてコピー
					auto dstsub = pathgen.getOutASSPath(-1, jktype);
					File::copy(srcsub, dstsub);
					outFilePath.outSubs.push_back(dstsub);
				}
			}
		}
		auto& capList = reformInfo_.getOutCaptionList(encoderIndex, videoFileIndex, cmtype);
		for (int lang = 0; lang < capList.size(); ++lang) {
			auto srcsub = setting_.getTmpASSFilePath(
				videoFileIndex, encoderIndex, lang, cmtype);
			if (setting_.getFormat() == FORMAT_MKV) {
				subsFiles.push_back(srcsub);
				subsTitles.push_back("ASS");
			}
			else { // MP4,M2TSの場合は別ファイルとしてコピー
				auto dstsub = pathgen.getOutASSPath(lang, (NicoJKType)0);
				File::copy(srcsub, dstsub);
				outFilePath.outSubs.push_back(dstsub);
			}
			subsFiles.push_back(setting_.getTmpSRTFilePath(
				videoFileIndex, encoderIndex, lang, cmtype));
			subsTitles.push_back("SRT");
		}

		// タイムコード用
		bool is120fps = (eoInfo.afsTimecode || setting_.isVFR120fps());
		std::pair<int, int> timebase = std::make_pair(vfmt.frameRateNum * (is120fps ? 4 : 2), vfmt.frameRateDenom);

		std::string tmpOutPath = setting_.getVfrTmpFilePath(videoFileIndex, encoderIndex, cmtype);
		outFilePath.outPath = pathgen.getOutFilePath();

		std::string metaFile;
		if (setting_.getFormat() == FORMAT_M2TS || setting_.getFormat() == FORMAT_TS) {
			// M2TS/TSの場合はmetaファイル作成
			StringBuilder sb;
			sb.append("MUXOPT\n");
			switch (eoInfo.format) {
			case VS_MPEG2:
				sb.append("V_MPEG-2");
				break;
			case VS_H264:
				sb.append("V_MPEG4/ISO/AVC");
				break;
			case VS_H265:
				sb.append("V_MPEGH/ISO/HEVC");
				break;
			}
			double fps = vfmt.frameRateNum / (double)vfmt.frameRateDenom;
			sb.append(", \"%s\", fps=%.3f\n", encVideoFile, fps);
			for (auto apath : audioFiles) {
				sb.append("A_AAC, \"%s\"\n", apath);
			}
			for (auto spath : subsFiles) {
				sb.append("S_TEXT/UTF8, \"%s\", fps=%.3f, video-width=%d, video-height=%d\n",
					spath, fps, vfmt.width, vfmt.height);
			}

			metaFile = setting_.getM2tsMetaFilePath(videoFileIndex, encoderIndex, cmtype);
			File file(metaFile, "w");
			file.write(sb.getMC());
		}

		auto args = makeMuxerArgs(
			setting_.getFormat(),
			setting_.getMuxerPath(), setting_.getTimelineEditorPath(), setting_.getMp4BoxPath(),
			encVideoFile, vfmt, audioFiles,
			outFilePath.outPath, tmpOutPath, chapterFile,
			outFilePath.tcPath, timebase, subsFiles, subsTitles, metaFile);

		for (int i = 0; i < (int)args.size(); ++i) {
			ctx.info(args[i].first.c_str());
			 StdRedirectedSubProcess muxer(args[i].first, 0, args[i].second);
			int ret = muxer.join();
			if (ret != 0) {
				THROWF(RuntimeException, "mux failed (exit code: %d)", ret);
			}
			// mp4boxがコンソール出力のコードページを変えてしまうので戻す
			ctx.setDefaultCP();
		}

		File outfile(outFilePath.outPath, "rb");
		outFilePath.fileSize = outfile.size();
	}

private:
	class SpDualMonoSplitter : public DualMonoSplitter
	{
		std::unique_ptr<File> file[2];
	public:
		SpDualMonoSplitter(AMTContext& ctx) : DualMonoSplitter(ctx) { }
		void open(int index, const std::string& filename) {
			file[index] = std::unique_ptr<File>(new File(filename, "wb"));
		}
		virtual void OnOutFrame(int index, MemoryChunk mc) {
			file[index]->write(mc);
		}
	};

	const ConfigWrapper& setting_;
	const StreamReformInfo& reformInfo_;

	PacketCache audioCache_;
};

class AMTSimpleMuxder : public AMTObject {
public:
	AMTSimpleMuxder(
		AMTContext&ctx,
		const ConfigWrapper& setting)
		: AMTObject(ctx)
		, setting_(setting)
		, totalOutSize_(0)
	{ }

	void mux(VideoFormat videoFormat, int audioCount) {
		// Mux
		std::vector<std::string> audioFiles;
		for (int i = 0; i < audioCount; ++i) {
			audioFiles.push_back(setting_.getIntAudioFilePath(0, 0, i, CMTYPE_BOTH));
		}
		std::string encVideoFile = setting_.getEncVideoFilePath(0, 0, CMTYPE_BOTH);
		std::string outFilePath = setting_.getOutFilePath(0, CMTYPE_BOTH);
		auto args = makeMuxerArgs(
			FORMAT_MP4,
			setting_.getMuxerPath(), setting_.getTimelineEditorPath(), setting_.getMp4BoxPath(),
			encVideoFile, videoFormat, audioFiles, outFilePath,
			std::string(), std::string(), std::string(), std::pair<int, int>(),
			std::vector<std::string>(), std::vector<std::string>(), std::string());
		ctx.info("[Mux開始]");
		ctx.info(args[0].first.c_str());

		{
			MySubProcess muxer(args[0].first);
			int ret = muxer.join();
			if (ret != 0) {
				THROWF(RuntimeException, "mux failed (muxer exit code: %d)", ret);
			}
		}

		{ // 出力サイズ取得
			File outfile(setting_.getOutFilePath(0, CMTYPE_BOTH), "rb");
			totalOutSize_ += outfile.size();
		}
	}

	int64_t getTotalOutSize() const {
		return totalOutSize_;
	}

private:
	class MySubProcess : public EventBaseSubProcess {
	public:
		MySubProcess(const std::string& args) : EventBaseSubProcess(args) { }
	protected:
		virtual void onOut(bool isErr, MemoryChunk mc) {
			// これはマルチスレッドで呼ばれるの注意
			fwrite(mc.data, mc.length, 1, isErr ? stderr : stdout);
			fflush(isErr ? stderr : stdout);
		}
	};

	const ConfigWrapper& setting_;
	int64_t totalOutSize_;
};
