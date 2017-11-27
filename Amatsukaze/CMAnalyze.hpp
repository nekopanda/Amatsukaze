/**
* Amtasukaze CM Analyze
* Copyright (c) 2017 Nekopanda
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

static void PrintFileAll(const std::string& path)
{
	File file(path, "rb");
	int sz = (int)file.size();
	if (sz == 0) return;
	auto buf = std::unique_ptr<uint8_t[]>(new uint8_t[sz]);
	auto rsz = file.read(MemoryChunk(buf.get(), sz));
	fwrite(buf.get(), 1, strnlen_s((char*)buf.get(), rsz), stderr);
}

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
    std::string avspath = makeAVSFile(videoFileIndex);

    // ロゴ解析
    if (setting_.getLogoPath().size() > 0) {
      ctx.info("[ロゴ解析]");
      sw.start();
      logoFrame(videoFileIndex, avspath);
      ctx.info("完了: %.2f秒", sw.getAndReset());

      if (logopath.size() > 0) {
        ctx.info("[ロゴ解析結果]");
        ctx.info("マッチしたロゴ: %s", logopath.c_str());
        PrintFileAll(setting_.getTmpLogoFramePath(videoFileIndex));
      }
    }

    // チャプター解析
    ctx.info("[無音・シーンチェンジ解析]");
    sw.start();
    chapterExe(videoFileIndex, avspath);
    ctx.info("完了: %.2f秒", sw.getAndReset());

    ctx.info("[無音・シーンチェンジ解析結果]");
    PrintFileAll(setting_.getTmpChapterExeOutPath(videoFileIndex));

    // CM推定
    ctx.info("[CM解析]");
    sw.start();
    joinLogoScp(videoFileIndex);
    ctx.info("完了: %.2f秒", sw.getAndReset());

    ctx.info("[CM解析結果 - TrimAVS]");
    PrintFileAll(setting_.getTmpTrimAVSPath(videoFileIndex));
    ctx.info("[CM解析結果 - 詳細]");
    PrintFileAll(setting_.getTmpJlsPath(videoFileIndex));

    // AVSファイルからCM区間を読む
    readTrimAVS(videoFileIndex, numFrames);
  }

  CMAnalyze(AMTContext& ctx,
    const ConfigWrapper& setting)
    : AMTObject(ctx)
    , setting_(setting)
  { }

  const std::string& getLogoPath() const {
    return logopath;
  }

  const std::vector<EncoderZone>& getZones() const {
    return cmzones;
  }

private:
  class MySubProcess : public EventBaseSubProcess {
  public:
    MySubProcess(const std::string& args, File* out = nullptr, File* err = nullptr)
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

  std::string logopath;
  std::vector<EncoderZone> cmzones;

  std::string makeAVSFile(int videoFileIndex)
  {
		StringBuilder sb;
		sb.append("LoadPlugin(\"%s\")\n", GetModulePath());
		sb.append("AMTSource(\"%s\")\n", setting_.getTmpAMTSourcePath(videoFileIndex));
		sb.append("Prefetch(1)\n", GetModulePath());
    std::string avspath = setting_.getTmpSourceAVSPath(videoFileIndex);
		File file(avspath, "w");
		file.write(sb.getMC());
    return avspath;
  }

  void logoFrame(int videoFileIndex, const std::string& avspath)
  {
    ScriptEnvironmentPointer env = make_unique_ptr(CreateScriptEnvironment2());

    try {
      AVSValue result;
      env->LoadPlugin(GetModulePath().c_str(), true, &result);
      PClip clip = env->Invoke("AMTSource", setting_.getTmpAMTSourcePath(videoFileIndex).c_str()).AsClip();

      auto vi = clip->GetVideoInfo();
      int duration = vi.num_frames * vi.fps_denominator / vi.fps_numerator;

      logo::LogoFrame logof(ctx, setting_.getLogoPath(), 0.1f);
      logof.scanFrames(clip, env.get());
      logof.writeResult(setting_.getTmpLogoFramePath(videoFileIndex));

      if (logof.getLogoRatio() < 0.5f) {
        // 3分以下のファイルはロゴが見つからなくても無視する
        if (duration <= 180) {
          ctx.info("マッチするロゴはありませんでしたが、動画の長さが%d秒(180秒以下)なので無視します", duration);
        }
        else if (!setting_.getIgnoreNoLogo()) {
          THROW(NoLogoException, "マッチするロゴが見つかりませんでした");
        }
      }
      else {
        logopath = setting_.getLogoPath()[logof.getBestLogo()];
      }
    }
    catch (const AvisynthError& avserror) {
      THROWF(AviSynthException, "%s", avserror.msg);
    }
  }

	std::string MakeChapterExeArgs(int videoFileIndex, const std::string& avspath)
	{
		return StringFormat("\"%s\" -v \"%s\" -o \"%s\"",
			setting_.getChapterExePath(), avspath,
			setting_.getTmpChapterExePath(videoFileIndex));
	}

	void chapterExe(int videoFileIndex, const std::string& avspath)
	{
		File stdoutf(setting_.getTmpChapterExeOutPath(videoFileIndex), "wb");
		auto args = MakeChapterExeArgs(videoFileIndex, avspath);
		ctx.info(args.c_str());
		MySubProcess process(args, &stdoutf);
		int exitCode = process.join();
		if (exitCode != 0) {
			THROWF(FormatException, "ChapterExeがエラーコード(%d)を返しました", exitCode);
		}
	}

	std::string MakeJoinLogoScpArgs(int videoFileIndex)
	{
		StringBuilder sb;
		sb.append("\"%s\"", setting_.getJoinLogoScpPath());
		if (logopath.size() > 0) {
			sb.append(" -inlogo \"%s\"", setting_.getTmpLogoFramePath(videoFileIndex));
		}
		sb.append(" -inscp \"%s\" -incmd \"%s\" -o \"%s\" -oscp \"%s\"",
			setting_.getTmpChapterExePath(videoFileIndex),
			setting_.getJoinLogoScpCmdPath(),
			setting_.getTmpTrimAVSPath(videoFileIndex),
			setting_.getTmpJlsPath(videoFileIndex));
		return sb.str();
	}

	void joinLogoScp(int videoFileIndex)
	{
		auto args = MakeJoinLogoScpArgs(videoFileIndex);
		ctx.info(args.c_str());
		MySubProcess process(args);
		int exitCode = process.join();
		if (exitCode != 0) {
			THROWF(FormatException, "join_logo_scp.exeがエラーコード(%d)を返しました", exitCode);
		}
	}

	void readTrimAVS(int videoFileIndex, int numFrames)
	{
		std::ifstream file(setting_.getTmpTrimAVSPath(videoFileIndex));
		std::string str;
		if (!std::getline(file, str)) {
			THROW(FormatException, "join_logo_scp.exeの出力AVSファイルが読めません");
		}

		std::regex re("Trim\\((\\d+),(\\d+)\\)");
		std::sregex_iterator iter(str.begin(), str.end(), re);
		std::sregex_iterator end;

		std::vector<int> split;
		split.push_back(0);
		for (; iter != end; ++iter) {
			split.push_back(std::stoi((*iter)[1].str()));
			split.push_back(std::stoi((*iter)[2].str()) + 1);
		}
		split.push_back(numFrames);

		for (int i = 1; i < (int)split.size(); ++i) {
			if (split[i] < split[i - 1]) {
				THROW(FormatException, "join_logo_scp.exeの出力AVSファイルが不正です");
			}
		}

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
		int videoFileIndex)
		: AMTObject(ctx)
		, setting(setting)
		, reformInfo(reformInfo)
	{
		makeBase(
			readTrimAVS(setting.getTmpTrimAVSPath(videoFileIndex)),
			readJls(setting.getTmpJlsPath(videoFileIndex)));
	}

	void exec(int videoFileIndex, int encoderIndex, CMType cmtype)
	{
		auto filechapters = makeFileChapter(videoFileIndex, encoderIndex, cmtype);
		writeChapter(filechapters, videoFileIndex, encoderIndex, cmtype);
	}

private:
	struct JlsElement {
		int frameStart;
		int frameEnd;
		int seconds;
		std::string comment;
		bool isCut;
		bool isCM;
	};

	const ConfigWrapper& setting;
	const StreamReformInfo& reformInfo;

	std::vector<JlsElement> chapters;

	std::vector<int> readTrimAVS(const std::string& trimpath)
	{
		std::ifstream file(trimpath);
		std::string str;
		if (!std::getline(file, str)) {
			THROW(FormatException, "join_logo_scp.exeの出力AVSファイルが読めません");
		}

		std::regex re("Trim\\((\\d+),(\\d+)\\)");
		std::sregex_iterator iter(str.begin(), str.end(), re);
		std::sregex_iterator end;

		std::vector<int> trims;
		for (; iter != end; ++iter) {
			trims.push_back(std::stoi((*iter)[1].str()));
			trims.push_back(std::stoi((*iter)[2].str()) + 1);
		}
		return trims;
	}

	std::vector<JlsElement> readJls(const std::string& jlspath)
	{
		std::ifstream file(jlspath);
		std::regex re("^\\s*(\\d+)\\s+(\\d+)\\s+(\\d+)\\s+([-\\d]+)\\s+(\\d+).*:(\\S+)");
		std::string str;
		std::vector<JlsElement> elements;
		while(std::getline(file, str)) {
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
				if (c.isCM) c.comment = "CM";
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

	std::vector<JlsElement> makeFileChapter(int videoFileIndex, int encoderIndex, CMType cmtype)
	{
		// 分割後のフレーム番号を取得
		auto& srcFrames = reformInfo.getFilterSourceFrames(videoFileIndex);
		std::vector<int> outFrames;
		for (int i = 0; i < (int)srcFrames.size(); ++i) {
			int frameEncoderIndex = reformInfo.getEncoderIndex(srcFrames[i].frameIndex);
			if (encoderIndex == frameEncoderIndex) {
        if (cmtype == CMTYPE_BOTH || cmtype == srcFrames[i].cmType) {
          outFrames.push_back(i);
        }
			}
		}

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
		auto& vfmt = reformInfo.getFormat(encoderIndex, videoFileIndex).videoFormat;
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

	void writeChapter(const std::vector<JlsElement>& chapters, int videoFileIndex, int encoderIndex, CMType cmtype)
	{
		auto& vfmt = reformInfo.getFormat(encoderIndex, videoFileIndex).videoFormat;
		float frameMs = (float)vfmt.frameRateDenom / vfmt.frameRateNum * 1000.0f;

		ctx.info("ファイル: %d-%d", videoFileIndex, encoderIndex);

		StringBuilder sb;
		int sumframes = 0;
		for (int i = 0; i < (int)chapters.size(); ++i) {
			auto& c = chapters[i];

			ctx.info("%5d: %s", c.frameStart, c.comment.c_str());

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

		File file(setting.getTmpChapterPath(videoFileIndex, encoderIndex, cmtype), "w");
		file.write(sb.getMC());
	}
};
