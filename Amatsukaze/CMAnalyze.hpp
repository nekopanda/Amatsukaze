/**
* Amtasukaze CM Analyze
* Copyright (c) 2017-2019 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include <fstream>
#include <string>
#include <iostream>
#include <memory>
#include <regex>

#include "StreamUtils.hpp"
#include "TranscodeSetting.hpp"
#include "LogoScan.hpp"
#include "ProcessThread.hpp"
#include "PerformanceUtil.hpp"

class CMAnalyze : public AMTObject
{
public:
	CMAnalyze(AMTContext& ctx,
		const ConfigWrapper& setting,
		int videoFileIndex, int numFrames)
		: AMTObject(ctx)
		, setting_(setting)
	{
		Stopwatch sw;
		tstring avspath = makeAVSFile(videoFileIndex);

		// ロゴ解析
		if (setting_.getLogoPath().size() > 0) {
			ctx.info("[ロゴ解析]");
			sw.start();
			logoFrame(videoFileIndex, avspath);
			ctx.infoF("完了: %.2f秒", sw.getAndReset());

			if (logopath.size() > 0) {
				ctx.info("[ロゴ解析結果]");
				ctx.infoF("マッチしたロゴ: %s", logopath.c_str());
				PrintFileAll(setting_.getTmpLogoFramePath(videoFileIndex));
			}
		}

		// チャプター解析
		ctx.info("[無音・シーンチェンジ解析]");
		sw.start();
		chapterExe(videoFileIndex, avspath);
		ctx.infoF("完了: %.2f秒", sw.getAndReset());

		ctx.info("[無音・シーンチェンジ解析結果]");
		PrintFileAll(setting_.getTmpChapterExeOutPath(videoFileIndex));

		// CM推定
		ctx.info("[CM解析]");
		sw.start();
		joinLogoScp(videoFileIndex);
		ctx.infoF("完了: %.2f秒", sw.getAndReset());

		ctx.info("[CM解析結果 - TrimAVS]");
		PrintFileAll(setting_.getTmpTrimAVSPath(videoFileIndex));
		ctx.info("[CM解析結果 - 詳細]");
		PrintFileAll(setting_.getTmpJlsPath(videoFileIndex));

		// AVSファイルからCM区間を読む
		readTrimAVS(videoFileIndex, numFrames);

		// シーンチェンジ
		readSceneChanges(videoFileIndex);

		// 分割情報
		readDiv(videoFileIndex, numFrames);

		makeCMZones(numFrames);
	}

	CMAnalyze(AMTContext& ctx,
		const ConfigWrapper& setting)
		: AMTObject(ctx)
		, setting_(setting)
	{ }

	const tstring& getLogoPath() const {
		return logopath;
	}

	const std::vector<int>& getTrims() const {
		return trims;
	}

	const std::vector<EncoderZone>& getZones() const {
		return cmzones;
	}

	const std::vector<int>& getDivs() const {
		return divs;
	}

	// PMT変更情報からCM追加認識
	void applyPmtCut(
		int numFrames, const double* rates,
		const std::vector<int>& pidChanges)
	{
		if (sceneChanges.size() == 0) {
			ctx.info("シーンチェンジ情報がないためPMT変更情報をCM判定に利用できません");
		}

		ctx.info("[PMT更新CM認識]");

		int validStart = 0, validEnd = numFrames;
		std::vector<int> matchedPoints;

		// picChangesに近いsceneChangesを見つける
		for (int i = 1; i < (int)pidChanges.size(); ++i) {
			int next = (int)(std::lower_bound(
				sceneChanges.begin(), sceneChanges.end(),
				pidChanges[i]) - sceneChanges.begin());
			int prev = next;
			if (next > 0) {
				prev = next - 1;
			}
			if (next == sceneChanges.size()) {
				next = prev;
			}
			//ctx.infoF("%d,%d,%d,%d,%d", pidChanges[i], next, sceneChanges[next], prev, sceneChanges[prev]);
			int diff = std::abs(pidChanges[i] - sceneChanges[next]);
			if (diff < 30 * 2) { // 次
				matchedPoints.push_back(sceneChanges[next]);
				ctx.infoF("フレーム%dのPMT変更はフレーム%dにシーンチェンジあり",
					pidChanges[i], sceneChanges[next]);
			}
			else {
				diff = std::abs(pidChanges[i] - sceneChanges[prev]);
				if (diff < 30 * 2) { // 前
					matchedPoints.push_back(sceneChanges[prev]);
					ctx.infoF("フレーム%dのPMT変更はフレーム%dにシーンチェンジあり",
						pidChanges[i], sceneChanges[prev]);
				}
				else {
					ctx.infoF("フレーム%dのPMT変更は付近にシーンチェンジがないため無視します", pidChanges[i]);
				}
			}
		}

		// 前後カット部分を算出
		int maxCutFrames0 = (int)(rates[0] * numFrames);
		int maxCutFrames1 = numFrames - (int)(rates[1] * numFrames);
		for (int i = 0; i < (int)matchedPoints.size(); ++i) {
			if (matchedPoints[i] < maxCutFrames0) {
				validStart = std::max(validStart, matchedPoints[i]);
			}
			if (matchedPoints[i] > maxCutFrames1) {
				validEnd = std::min(validEnd, matchedPoints[i]);
			}
		}
		ctx.infoF("設定区間: 0-%d %d-%d", maxCutFrames0, maxCutFrames1, numFrames);
		ctx.infoF("検出CM区間: 0-%d %d-%d", validStart, validEnd, numFrames);

		// trimsに反映
		auto copy = trims;
		trims.clear();
		for (int i = 0; i < (int)copy.size(); i += 2) {
			auto start = copy[i];
			auto end = copy[i + 1];
			if (end <= validStart) {
				// 開始前
				continue;
			}
			else if (start <= validStart) {
				// 途中から開始
				start = validStart;
			}
			if (start >= validEnd) {
				// 終了後
				continue;
			}
			else if (end >= validEnd) {
				// 途中で終了
				end = validEnd;
			}
			trims.push_back(start);
			trims.push_back(end);
		}

		// cmzonesに反映
		makeCMZones(numFrames);
	}

	void inputTrimAVS(int numFrames, const tstring& trimavsPath)
	{
		ctx.infoF("[Trim情報入力]: %s", trimavsPath.c_str());
		PrintFileAll(trimavsPath);

		// AVSファイルからCM区間を読む
		File file(trimavsPath, _T("r"));
		std::string str;
		if (!file.getline(str)) {
			THROW(FormatException, "TrimAVSファイルが読めません");
		}
		readTrimAVS(str, numFrames);

		// cmzonesに反映
		makeCMZones(numFrames);
	}

private:
	class MySubProcess : public EventBaseSubProcess {
	public:
		MySubProcess(const tstring& args, File* out = nullptr, File* err = nullptr)
			: EventBaseSubProcess(args)
			, out(out)
			, err(err)
		{ }
	protected:
		File* out;
		File* err;
		virtual void onOut(bool isErr, MemoryChunk mc) {
			// これはマルチスレッドで呼ばれるの注意
			File* dst = isErr ? err : out;
			if (dst != nullptr) {
				dst->write(mc);
			}
			else {
				fwrite(mc.data, mc.length, 1, isErr ? stderr : stdout);
				fflush(isErr ? stderr : stdout);
			}
		}
	};

	const ConfigWrapper& setting_;

	tstring logopath;
	std::vector<int> trims;
	std::vector<EncoderZone> cmzones;
	std::vector<int> sceneChanges;
	std::vector<int> divs;

	tstring makeAVSFile(int videoFileIndex)
	{
		StringBuilder sb;
		
		// オートロードプラグインのロードに失敗すると動作しなくなるのでそれを回避
		sb.append("ClearAutoloadDirs()\n");

		sb.append("LoadPlugin(\"%s\")\n", GetModulePath());
		sb.append("AMTSource(\"%s\")\n", setting_.getTmpAMTSourcePath(videoFileIndex));
		sb.append("Prefetch(1)\n");
		tstring avspath = setting_.getTmpSourceAVSPath(videoFileIndex);
		File file(avspath, _T("w"));
		file.write(sb.getMC());
		return avspath;
	}

	std::string makePreamble() {
		StringBuilder sb;
		// システムのプラグインフォルダを無効化
		if (setting_.isSystemAvsPlugin() == false) {
			sb.append("ClearAutoloadDirs()\n");
		}
		// Amatsukaze用オートロードフォルダを追加
		sb.append("AddAutoloadDir(\"%s\\plugins64\")\n", GetModuleDirectory());
		return sb.str();
	}

	void logoFrame(int videoFileIndex, const tstring& avspath)
	{
		ScriptEnvironmentPointer env = make_unique_ptr(CreateScriptEnvironment2());

		try {
			AVSValue result;
			env->Invoke("Eval", AVSValue(makePreamble().c_str()));
			env->LoadPlugin(to_string(GetModulePath()).c_str(), true, &result);
			PClip clip = env->Invoke("AMTSource", to_string(setting_.getTmpAMTSourcePath(videoFileIndex)).c_str()).AsClip();

			auto vi = clip->GetVideoInfo();
			int duration = vi.num_frames * vi.fps_denominator / vi.fps_numerator;

			logo::LogoFrame logof(ctx, setting_.getLogoPath(), 0.35f);
			logof.scanFrames(clip, env.get());
#if 0
			logof.dumpResult(setting_.getTmpLogoFramePath(videoFileIndex));
#endif
			logof.writeResult(setting_.getTmpLogoFramePath(videoFileIndex));

			float threshold = setting_.isLooseLogoDetection() ? 0.03f : (duration <= 60 * 7) ? 0.03f : 0.1f;
			if (logof.getLogoRatio() < threshold) {
				ctx.info("この区間はマッチするロゴはありませんでした");
			}
			else {
				logopath = setting_.getLogoPath()[logof.getBestLogo()];
			}
		}
		catch (const AvisynthError& avserror) {
			THROWF(AviSynthException, "%s", avserror.msg);
		}
	}

	tstring MakeChapterExeArgs(int videoFileIndex, const tstring& avspath)
	{
		return StringFormat(_T("\"%s\" -v \"%s\" -o \"%s\" %s"),
			setting_.getChapterExePath(), avspath,
			setting_.getTmpChapterExePath(videoFileIndex),
			setting_.getChapterExeOptions());
	}

	void chapterExe(int videoFileIndex, const tstring& avspath)
	{
		File stdoutf(setting_.getTmpChapterExeOutPath(videoFileIndex), _T("wb"));
		auto args = MakeChapterExeArgs(videoFileIndex, avspath);
		ctx.infoF("%s", args);
		MySubProcess process(args, &stdoutf);
		int exitCode = process.join();
		if (exitCode != 0) {
			THROWF(FormatException, "ChapterExeがエラーコード(%d)を返しました", exitCode);
		}
	}

	tstring MakeJoinLogoScpArgs(int videoFileIndex)
	{
		StringBuilderT sb;
		sb.append(_T("\"%s\""), setting_.getJoinLogoScpPath());
		if (logopath.size() > 0) {
			sb.append(_T(" -inlogo \"%s\""), setting_.getTmpLogoFramePath(videoFileIndex));
		}
		sb.append(_T(" -inscp \"%s\" -incmd \"%s\" -o \"%s\" -oscp \"%s\" -odiv \"%s\" %s"),
			setting_.getTmpChapterExePath(videoFileIndex),
			setting_.getJoinLogoScpCmdPath(),
			setting_.getTmpTrimAVSPath(videoFileIndex),
			setting_.getTmpJlsPath(videoFileIndex),
      setting_.getTmpDivPath(videoFileIndex),
			setting_.getJoinLogoScpOptions());
		return sb.str();
	}

	void joinLogoScp(int videoFileIndex)
	{
		auto args = MakeJoinLogoScpArgs(videoFileIndex);
		ctx.infoF("%s", args);
		MySubProcess process(args);
		int exitCode = process.join();
		if (exitCode != 0) {
			THROWF(FormatException, "join_logo_scp.exeがエラーコード(%d)を返しました", exitCode);
		}
	}

	void readTrimAVS(int videoFileIndex, int numFrames)
	{
		File file(setting_.getTmpTrimAVSPath(videoFileIndex), _T("r"));
		std::string str;
		if (!file.getline(str)) {
			THROW(FormatException, "join_logo_scp.exeの出力AVSファイルが読めません");
		}
		readTrimAVS(str, numFrames);
	}

	void readTrimAVS(std::string str, int numFrames)
	{
		std::transform(str.begin(), str.end(), str.begin(), ::tolower);
		std::regex re("trim\\s*\\(\\s*(\\d+)\\s*,\\s*(\\d+)\\s*\\)");
		std::sregex_iterator iter(str.begin(), str.end(), re);
		std::sregex_iterator end;

		trims.clear();
		for (; iter != end; ++iter) {
			trims.push_back(std::stoi((*iter)[1].str()));
			trims.push_back(std::stoi((*iter)[2].str()) + 1);
		}
	}

	void readDiv(int videoFileIndex, int numFrames)
	{
		File file(setting_.getTmpDivPath(videoFileIndex), _T("r"));
		std::string str;
		divs.clear();
		while (file.getline(str)) {
			if (str.size()) {
				divs.push_back(std::atoi(str.c_str()));
			}
		}
		// 正規化
		if (divs.size() == 0) {
			divs.push_back(0);
		}
		if (divs.front() != 0) {
			divs.insert(divs.begin(), 0);
		}
		divs.push_back(numFrames);
	}

	void readSceneChanges(int videoFileIndex)
	{
		File file(setting_.getTmpChapterExeOutPath(videoFileIndex), _T("r"));
		std::string str;

		// ヘッダ部分をスキップ
		while (1) {
			if (!file.getline(str)) {
				THROW(FormatException, "ChapterExe.exeの出力ファイルが読めません");
			}
			if (starts_with(str, "----")) {
				break;
			}
		}

		std::regex re0("mute\\s*(\\d+):\\s*(\\d+)\\s*-\\s*(\\d+).*");
		std::regex re1("\\s*SCPos:\\s*(\\d+).*");

		while (file.getline(str)) {
			std::smatch m;
			if (std::regex_search(str, m, re0)) {
				//std::stoi(m[1].str());
				//std::stoi(m[2].str());
			}
			else if (std::regex_search(str, m, re1)) {
				sceneChanges.push_back(std::stoi(m[1].str()));
			}
		}
	}

	void makeCMZones(int numFrames) {
		std::deque<int> split(trims.begin(), trims.end());
		split.push_front(0);
		split.push_back(numFrames);

		for (int i = 1; i < (int)split.size(); ++i) {
			if (split[i] < split[i - 1]) {
				THROW(FormatException, "join_logo_scp.exeの出力AVSファイルが不正です");
			}
		}

		cmzones.clear();
		for (int i = 0; i < (int)split.size(); i += 2) {
			EncoderZone zone = { split[i], split[i + 1] };
			if (zone.endFrame - zone.startFrame > 30) { // 短すぎる区間は捨てる
				cmzones.push_back(zone);
			}
		}
	}
};

class MakeChapter : public AMTObject
{
public:
	MakeChapter(AMTContext& ctx,
		const ConfigWrapper& setting,
		const StreamReformInfo& reformInfo,
		int videoFileIndex,
		const std::vector<int>& trims)
		: AMTObject(ctx)
		, setting(setting)
		, reformInfo(reformInfo)
	{
		makeBase(trims, readJls(setting.getTmpJlsPath(videoFileIndex)));
	}

	void exec(EncodeFileKey key)
	{
		auto filechapters = makeFileChapter(key);
		if (filechapters.size() > 0) {
			writeChapter(filechapters, key);
		}
	}

private:
	struct JlsElement {
		int frameStart;
		int frameEnd;
		int seconds;
		std::string comment;
		bool isCut;
		bool isCM;
		bool isOld;
	};

	const ConfigWrapper& setting;
	const StreamReformInfo& reformInfo;

	std::vector<JlsElement> chapters;

	std::vector<JlsElement> readJls(const tstring& jlspath)
	{
		File file(jlspath, _T("r"));
		std::regex re("^\\s*(\\d+)\\s+(\\d+)\\s+(\\d+)\\s+([-\\d]+)\\s+(\\d+).*:(\\S+)");
		std::regex reOld("^\\s*(\\d+)\\s+(\\d+)\\s+(\\d+)\\s+([-\\d]+)\\s+(\\d+)");
		std::string str;
		std::vector<JlsElement> elements;
		while (file.getline(str)) {
			std::smatch m;
			if (std::regex_search(str, m, re)) {
				JlsElement elem = {
					std::stoi(m[1].str()),
					std::stoi(m[2].str()) + 1,
					std::stoi(m[3].str()),
					m[6].str()
				};
				elements.push_back(elem);
			}
			else if (std::regex_search(str, m, reOld)) {
				JlsElement elem = {
					std::stoi(m[1].str()),
					std::stoi(m[2].str()) + 1,
					std::stoi(m[3].str()),
					""
				};
				elements.push_back(elem);
			}
		}
		return elements;
	}

	static bool startsWith(const std::string& s, const std::string& prefix) {
		auto size = prefix.size();
		if (s.size() < size) return false;
		return std::equal(std::begin(prefix), std::end(prefix), std::begin(s));
	}

	void makeBase(std::vector<int> trims, std::vector<JlsElement> elements)
	{
		// isCut, isCMフラグを生成
		for (int i = 0; i < (int)elements.size(); ++i) {
			auto& e = elements[i];
			int trimIdx = (int)(std::lower_bound(trims.begin(), trims.end(), (e.frameStart + e.frameEnd) / 2) - trims.begin());
			e.isCut = !(trimIdx % 2);
			e.isCM = (e.comment == "CM");
			e.isOld = (e.comment.size() == 0);
		}

		// 余分なものはマージ
		JlsElement cur = elements[0];
		for (int i = 1; i < (int)elements.size(); ++i) {
			auto& e = elements[i];
			bool isMerge = false;
			if (cur.isCut && e.isCut) {
				if (cur.isCM == e.isCM) {
					isMerge = true;
				}
			}
			if (isMerge) {
				cur.frameEnd = e.frameEnd;
				cur.seconds += e.seconds;
			}
			else {
				chapters.push_back(cur);
				cur = e;
			}
		}
		chapters.push_back(cur);

		// コメントをチャプター名に変更
		int nChapter = -1;
		bool prevCM = true;
		for (int i = 0; i < (int)chapters.size(); ++i) {
			auto& c = chapters[i];
			if (c.isCut) {
				if (c.isCM || c.isOld) c.comment = "CM";
				else c.comment = "CM?";
				prevCM = true;
			}
			else {
				bool showSec = false;
				if (startsWith(c.comment, "Trailer") ||
					startsWith(c.comment, "Sponsor") ||
					startsWith(c.comment, "Endcard") ||
					startsWith(c.comment, "Edge") ||
					startsWith(c.comment, "Border") ||
					c.seconds == 60 ||
					c.seconds == 90)
				{
					showSec = true;
				}
				if (prevCM) {
					++nChapter;
					prevCM = false;
				}
				c.comment = 'A' + (nChapter % 26);
				if (showSec) {
					c.comment += std::to_string(c.seconds) + "Sec";
				}
			}
		}
	}

	std::vector<JlsElement> makeFileChapter(EncodeFileKey key)
	{
		const auto& outFrames = reformInfo.getEncodeFile(key).videoFrames;

		// チャプターを分割後のフレーム番号に変換
		std::vector<JlsElement> cvtChapters;
		for (int i = 0; i < (int)chapters.size(); ++i) {
			const auto& c = chapters[i];
			JlsElement fc = c;
			fc.frameStart = (int)(std::lower_bound(outFrames.begin(), outFrames.end(), c.frameStart) - outFrames.begin());
			fc.frameEnd = (int)(std::lower_bound(outFrames.begin(), outFrames.end(), c.frameEnd) - outFrames.begin());
			cvtChapters.push_back(fc);
		}

		// 短すぎるチャプターは消す
		auto& vfmt = reformInfo.getFormat(key).videoFormat;
		int fps = (int)std::round((float)vfmt.frameRateNum / vfmt.frameRateDenom);
		std::vector<JlsElement> fileChapters;
		JlsElement cur = { 0 };
		for (int i = 0; i < (int)cvtChapters.size(); ++i) {
			auto& c = cvtChapters[i];
			if (c.frameEnd - c.frameStart < fps * 2) { // 2秒以下のチャプターは消す
				cur.frameEnd = c.frameEnd;
			}
			else if (cur.comment.size() == 0) {
				// まだ中身を入れていない場合は今のチャプターを入れる
				int start = cur.frameStart;
				cur = c;
				cur.frameStart = start;
			}
			else {
				// もう中身が入っているので、出力
				fileChapters.push_back(cur);
				cur = c;
			}
		}
		if (cur.comment.size() > 0) {
			fileChapters.push_back(cur);
		}

		return fileChapters;
	}

	void writeChapter(const std::vector<JlsElement>& chapters, EncodeFileKey key)
	{
		auto& vfmt = reformInfo.getFormat(key).videoFormat;
		float frameMs = (float)vfmt.frameRateDenom / vfmt.frameRateNum * 1000.0f;

		ctx.infoF("ファイル: %d-%d-%d %s", key.video, key.format, key.div, CMTypeToString(key.cm));

		StringBuilder sb;
		int sumframes = 0;
		for (int i = 0; i < (int)chapters.size(); ++i) {
			auto& c = chapters[i];

			ctx.infoF("%5d: %s", c.frameStart, c.comment.c_str());

			int ms = (int)std::round(sumframes * frameMs);
			int s = ms / 1000;
			int m = s / 60;
			int h = m / 60;
			int ss = ms % 1000;
			s %= 60;
			m %= 60;
			h %= 60;

			sb.append("CHAPTER%02d=%02d:%02d:%02d.%03d\n", (i + 1), h, m, s, ss);
			sb.append("CHAPTER%02dNAME=%s\n", (i + 1), c.comment);

			sumframes += c.frameEnd - c.frameStart;
		}

		File file(setting.getTmpChapterPath(key), _T("w"));
		file.write(sb.getMC());
	}
};
