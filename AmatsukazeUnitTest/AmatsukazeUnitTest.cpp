/**
* Amtasukaze Unit Test
* Copyright (c) 2017-2018 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#define _CRT_SECURE_NO_WARNINGS

#include <string>
#include <cmath>
#include <memory>

#include "gtest/gtest.h"

#define AVS_LINKAGE_DLLIMPORT
#include "avisynth.h"
#pragma comment(lib, "avisynth.lib")

#include <Windows.h>

__declspec(dllimport) int AmatsukazeCLI(int argc, const wchar_t* argv[]);

static bool fileExists(const wchar_t* filepath) {
	WIN32_FIND_DATAW findData;
	HANDLE hFind = FindFirstFileW(filepath, &findData);
	if (hFind == INVALID_HANDLE_VALUE) {
		return false;
	}
	FindClose(hFind);
	return true;
}

struct ScriptEnvironmentDeleter {
	void operator()(IScriptEnvironment* env) {
		env->DeleteScriptEnvironment();
	}
};

typedef std::unique_ptr<IScriptEnvironment2, ScriptEnvironmentDeleter> PEnv;

#define LEN(arr) (sizeof(arr) / sizeof(arr[0]))

std::string GetDirectoryName(const std::string& filename)
{
	std::string directory;
	const size_t last_slash_idx = filename.rfind('\\');
	if (std::string::npos != last_slash_idx)
	{
		directory = filename.substr(0, last_slash_idx);
	}
	return directory;
}

// テスト対象となるクラス Foo のためのフィクスチャ
class TestBase : public ::testing::Test {
protected:

	TestBase() {
		char buf[MAX_PATH];
		GetModuleFileName(nullptr, buf, MAX_PATH);
		modulePath = GetDirectoryName(buf);

		wchar_t curdir[200];
		GetCurrentDirectoryW(sizeof(curdir), curdir);
		std::wstring inipath = curdir;
		inipath += L"\\TestParam.ini";

		getParam(inipath, L"TestDataDir", TestDataDir);
		getParam(inipath, L"TestWorkDir", TestWorkDir);
		getParam(inipath, L"MPEG2VideoTsFile", MPEG2VideoTsFile);
		getParam(inipath, L"H264VideoTsFile", H264VideoTsFile);
		getParam(inipath, L"OneSegVideoTsFile", OneSegVideoTsFile);
		getParam(inipath, L"SampleAACFile", SampleAACFile);
		getParam(inipath, L"SampleMPEG2PsFile", SampleMPEG2PsFile);
		getParam(inipath, L"VideoFormatChangeTsFile", VideoFormatChangeTsFile);
		getParam(inipath, L"AudioFormatChangeTsFile", AudioFormatChangeTsFile);
		getParam(inipath, L"MultiAudioTsFile", MultiAudioTsFile);
		getParam(inipath, L"RffFieldPictureTsFile", RffFieldPictureTsFile);
		getParam(inipath, L"DropTsFile", DropTsFile);
		getParam(inipath, L"VideoDropTsFile", VideoDropTsFile);
		getParam(inipath, L"AudioDropTsFile", AudioDropTsFile);
		getParam(inipath, L"PullDownTsFile", PullDownTsFile);
		getParam(inipath, L"DameMojiTsFile", DameMojiTsFile);
		getParam(inipath, L"LargeTsFile", LargeTsFile);
	}

	virtual ~TestBase() {
		// テスト毎に実行される，例外を投げない clean-up をここに書きます．
	}

	// コンストラクタとデストラクタでは不十分な場合．
	// 以下のメソッドを定義することができます：

	virtual void SetUp() {
		// このコードは，コンストラクタの直後（各テストの直前）
		// に呼び出されます．
	}

	virtual void TearDown() {
		// このコードは，各テストの直後（デストラクタの直前）
		// に呼び出されます．
	}

	// ここで宣言されるオブジェクトは，テストケース内の全てのテストで利用できます．
	std::string modulePath;

	std::wstring TestDataDir;
	std::wstring TestWorkDir;
	std::wstring MPEG2VideoTsFile;
	std::wstring H264VideoTsFile;
	std::wstring OneSegVideoTsFile;
	std::wstring SampleAACFile;
	std::wstring SampleMPEG2PsFile;
	std::wstring VideoFormatChangeTsFile;
	std::wstring AudioFormatChangeTsFile;
	std::wstring MultiAudioTsFile;
	std::wstring RffFieldPictureTsFile;
	std::wstring DropTsFile;
	std::wstring VideoDropTsFile;
	std::wstring AudioDropTsFile;
	std::wstring PullDownTsFile;
	std::wstring DameMojiTsFile;
	std::wstring LargeTsFile;

	void ParserTest(const std::wstring& filename, bool verify = true);

	void EncoderOptionTest(const wchar_t* option) {
		printf("Option: %ls\n", option);
		const wchar_t* args[] = {
			L"AmatsukazeTest.exe", L"--mode", L"test_eo", L"-e", L"QSVEnc",
			L"-eo", option,
		};
		EXPECT_EQ(AmatsukazeCLI(LEN(args), args), 0);
	}

private:
	void getParam(const std::wstring& inipath, const wchar_t* key, std::wstring& dst) {
		wchar_t buf[200];
		if (GetPrivateProfileStringW(L"FILE", key, L"", buf, sizeof(buf), inipath.c_str())) {
			dst = buf;
		}
	}
};

TEST(CRC, PrintTable)
{
	const wchar_t* args[] = { L"AmatsukazeTest.exe", L"--mode", L"test_print_crc" };
	EXPECT_EQ(AmatsukazeCLI(LEN(args), args), 0);
}

TEST(CRC, CheckCRC)
{
	const wchar_t* args[] = { L"AmatsukazeTest.exe", L"--mode", L"test_crc" };
	EXPECT_EQ(AmatsukazeCLI(LEN(args), args), 0);
}

TEST(Util, readOpt)
{
	const wchar_t* args[] = { L"AmatsukazeTest.exe", L"--mode", L"test_read_bits" };
	EXPECT_EQ(AmatsukazeCLI(LEN(args), args), 0);
}

TEST(Util, AutoBufferTest)
{
	const wchar_t* args[] = { L"AmatsukazeTest.exe", L"--mode", L"test_auto_buffer" };
	EXPECT_EQ(AmatsukazeCLI(LEN(args), args), 0);
}

void VerifyMpeg2Ps(std::wstring srcfile)
{
	const wchar_t* args[] = { L"AmatsukazeTest.exe", L"--mode", L"test_verifympeg2ps", L"-i", srcfile.c_str() };
	EXPECT_EQ(AmatsukazeCLI(LEN(args), args), 0);
}

void TestBase::ParserTest(const std::wstring& filename, bool verify) {
	std::wstring srcDir = TestDataDir + L"\\";
	std::wstring dstDir = TestWorkDir + L"\\";

	std::wstring srcfile = srcDir + filename + L".ts";
	std::wstring outfile = dstDir + filename + L".mp4";

	if (filename.size() == 0 || !fileExists(srcfile.c_str())) {
		fprintf(stderr, "テストファイルがないのでスキップ: %ls\n", srcfile.c_str());
		return;
	}

	const wchar_t* args[] = {
		L"AmatsukazeTest.exe", L"--mode", L"test_readts", 
		L"-i", srcfile.c_str(),
		L"-o", outfile.c_str(),
		L"-w", dstDir.c_str(),
	};
	EXPECT_EQ(AmatsukazeCLI(LEN(args), args), 0);

	// 出力ファイルをチェック
	//if (verify) {
	//	VerifyMpeg2Ps(mpgfile);
	//}
}

TEST_F(TestBase, MPEG2Parser) {
	ParserTest(MPEG2VideoTsFile);
}

TEST_F(TestBase, H264Parser) {
	ParserTest(H264VideoTsFile);
}

TEST_F(TestBase, H264Parser1Seg) {
	ParserTest(OneSegVideoTsFile);
}

TEST_F(TestBase, Pulldown) {
	ParserTest(PullDownTsFile);
}

// TODO: 通常はオフ
TEST_F(TestBase, LargeTsParse) {
	ParserTest(LargeTsFile, false);
}

TEST_F(TestBase, MPEG2PSVerifier) {
	std::wstring srcfile = TestDataDir + L"\\" + SampleMPEG2PsFile + L".mpg";
	VerifyMpeg2Ps(srcfile);
}

// FAADデコードが正しい出力をするかテスト
TEST_F(TestBase, AacDecodeVerifyTest) {
	std::wstring srcDir = TestDataDir + L"\\";
	std::wstring filename = SampleAACFile;
	std::wstring srcfile = srcDir + filename + L".aac";
	std::wstring testfile = srcDir + filename + L".wav";

	if (filename.size() == 0 || !fileExists(srcfile.c_str())) {
		printf("テストファイルがないのでスキップ: %ls\n", srcfile.c_str());
		return;
	}
	if (!fileExists(testfile.c_str())) {
		printf("テストファイルがないのでスキップ: %ls\n", testfile.c_str());
		return;
	}

	const wchar_t* args[] = { L"AmatsukazeTest.exe", L"--mode", L"test_aacdec", L"-i", filename.c_str() };
	EXPECT_EQ(AmatsukazeCLI(LEN(args), args), 0);
}

TEST_F(TestBase, WaveWriter) {
	std::wstring dstDir = TestWorkDir + L"\\";
	std::wstring dstfile = dstDir + L"fake.wav";

	const wchar_t* args[] = { L"AmatsukazeTest.exe", L"--mode", L"test_aacdec", L"-o", dstfile.c_str() };
	EXPECT_EQ(AmatsukazeCLI(LEN(args), args), 0);
}

// Process/Thread Test

TEST(Process, SimpleProcessTest)
{
	const wchar_t* args[] = { L"AmatsukazeTest.exe", L"--mode", L"test_process" };
	EXPECT_EQ(AmatsukazeCLI(LEN(args), args), 0);
}

// Encode Test

TEST_F(TestBase, encodeMpeg2Test)
{
	std::wstring srcDir = TestDataDir + L"\\";
	std::wstring dstDir = TestWorkDir + L"\\";
	std::wstring srcPath = srcDir + LargeTsFile;
	std::wstring dstPath = dstDir + L"Mpeg2Test";

	const wchar_t* args[] = {
		L"AmatsukazeTest.exe", L"--mode", L"ts",
		L"-i", srcPath.c_str(),
		L"-o", dstPath.c_str(),
		L"-w", dstDir.c_str(),
		L"-eo", L"--preset superfast --crf 23"
	};
	EXPECT_EQ(AmatsukazeCLI(LEN(args), args), 0);
}

TEST_F(TestBase, fileStreamInfoTest)
{
	std::wstring srcDir = TestDataDir + L"\\";
	std::wstring dstDir = TestWorkDir + L"\\";
	std::wstring srcPath = srcDir + LargeTsFile;
	std::wstring dstPath = dstDir + LargeTsFile;

	const wchar_t* args[] = {
		L"AmatsukazeTest.exe", L"--mode", L"test_streamreform",
		L"-i", srcPath.c_str(),
		L"-o", dstPath.c_str(),
		L"-w", dstDir.c_str(),
		L"-eo", L"--preset superfast --crf 23"
	};
	EXPECT_EQ(AmatsukazeCLI(LEN(args), args), 0);
}

TEST_F(TestBase, DamemojiTest)
{
	std::wstring srcDir = TestDataDir + L"\\";
	std::wstring dstDir = TestWorkDir + L"ー ソ\\十 表\\";
	std::wstring srcPath = srcDir + LargeTsFile;
	std::wstring dstPath = dstDir + L"Mpeg2Test";

	const wchar_t* args[] = {
		L"AmatsukazeTest.exe", L"--mode", L"test_streamreform",
		L"-i", srcPath.c_str(),
		L"-o", dstPath.c_str(),
		L"-w", dstDir.c_str(),
		L"-eo", L"--preset superfast --crf 23"
	};
	EXPECT_EQ(AmatsukazeCLI(LEN(args), args), 0);
}

TEST_F(TestBase, LosslessTest)
{
	std::wstring srcDir = TestDataDir + L"\\";
	std::wstring dstDir = TestWorkDir + L"\\";
	std::wstring inavs = srcDir + L"input.avs";
	std::wstring dstPath = dstDir + L"lossless.utv";

	const wchar_t* args[] = {
		L"AmatsukazeTest.exe", L"--mode", L"test_lossless",
		L"-o", dstPath.c_str(),
		L"-f", inavs.c_str()
	};
	EXPECT_EQ(AmatsukazeCLI(LEN(args), args), 0);
}

TEST_F(TestBase, LogoFrameTest)
{
	std::wstring srcDir = TestDataDir + L"\\";
	std::wstring dstDir = TestWorkDir + L"\\";
	std::wstring inavs = srcDir + L"input.avs";
	std::wstring outtxt = dstDir + L"logoframe.txt";

	const wchar_t* args[] = {
		L"AmatsukazeTest.exe", L"--mode", L"test_logoframe",
		L"--logo", L"logo\\SID410-1.lgd",
		L"--logo", L"logo\\SID410-2.lgd",
		L"-a", outtxt.c_str(),
		L"-f", inavs.c_str()
	};
	EXPECT_EQ(AmatsukazeCLI(LEN(args), args), 0);
}

TEST_F(TestBase, SplitDualMonoAAC)
{
	std::wstring srcDir = TestDataDir + L"\\";
	std::wstring dstDir = TestWorkDir + L"\\";
	std::wstring inaac = srcDir + L"dualmono.aac";

	const wchar_t* args[] = {
		L"AmatsukazeTest.exe", L"--mode", L"test_dualmono",
		L"-i", inaac.c_str(),
		L"-w", dstDir.c_str(),
	};
	EXPECT_EQ(AmatsukazeCLI(LEN(args), args), 0);
}

TEST_F(TestBase, AACDecodeTest)
{
	std::wstring srcDir = TestDataDir + L"\\";
	std::wstring dstDir = TestWorkDir + L"\\";
	std::wstring inaac = srcDir + L"a0-0-1.aac";

	const wchar_t* args[] = {
		L"AmatsukazeTest.exe", L"--mode", L"test_aacdecode",
		L"-i", inaac.c_str(),
		L"-w", dstDir.c_str(),
	};
	EXPECT_EQ(AmatsukazeCLI(LEN(args), args), 0);
}

TEST_F(TestBase, CaptionASSTest)
{
	std::wstring srcDir = TestDataDir + L"\\";
	std::wstring dstDir = TestWorkDir + L"\\";

	std::wstring srcfile = srcDir + MPEG2VideoTsFile + L".ts";
	std::wstring outfile = dstDir + MPEG2VideoTsFile + L".mp4";

	const wchar_t* args[] = {
		L"AmatsukazeTest.exe", L"--mode", L"test_ass",
		L"-i", srcfile.c_str(),
		L"-o", outfile.c_str(),
		L"-w", dstDir.c_str(),
	};
	EXPECT_EQ(AmatsukazeCLI(LEN(args), args), 0);
}

TEST_F(TestBase, EncoderOptionTest01)
{
	EncoderOptionTest(L"--vpp-deinterlace none");
}
TEST_F(TestBase, EncoderOptionTest02)
{
	EncoderOptionTest(L"--vpp-deinterlace normal");
}
TEST_F(TestBase, EncoderOptionTest03)
{
	EncoderOptionTest(L"--vpp-deinterlace adaptive");
}
TEST_F(TestBase, EncoderOptionTest04)
{
	EncoderOptionTest(L"--vpp-deinterlace bob");
}
TEST_F(TestBase, EncoderOptionTest05)
{
	EncoderOptionTest(L"--vpp-afs preset=anime,24fps=true,rff=true");
}
TEST_F(TestBase, EncoderOptionTest06)
{
	EncoderOptionTest(L"--vpp-afs preset=anime");
}
TEST_F(TestBase, EncoderOptionTest07)
{
	EncoderOptionTest(L"--vpp-afs preset=24fps");
}
TEST_F(TestBase, EncoderOptionTest08)
{
	EncoderOptionTest(L"--vpp-afs 24fps=true,preset=anime");
}
TEST_F(TestBase, EncoderOptionTest09)
{
	EncoderOptionTest(L"-i %1 --avqsv --cqp 22:24:26 -u best --output-res 1280x720 --vpp-denoise 20 --tff --vpp-deinterlace normal --trellis auto --bframes 2 --gop-len 300 --audio-codec aac --audio-bitrate 128 -o \"dpn1.mp4\" --vpp-afs rff=true,24fps=true");
}

TEST(CLI, ArgumentTest)
{
	const wchar_t* argv[] = {
		L"AmatsukazeTest.exe",
		L"-s",
		L"12345",
		L"-i",
		L"C:\\hoge\\input.ts",
		L"-o",
		L"C:\\oops\\output.mmp4",
		L"-w",
		L"C:\\hoge\\",
		L"-et",
		L"x265",
		L"--dump",
		L"-e",
		L"D:\\program\\revXXX-x265.exe",
		L"-eo",
		L"--preset slow --profile main --crf 23 --qcomp 0.7 --vbv-bufsize 10000 --vbv-maxrate 10000 --keyint -1 --min-keyint 4 --b-pyramid none --partitions p8x8,b8x8,i4x4 --ref 3 --weightp 0 --level 3",
		L"-m",
		L"D:\\program\\revXXX-muxer.exe",
		L"-t",
		L"D:\\program\\timelineditro.exe",
		L"-j",
		L"JJJJJJJJSON.json",
		L"--mode",
		L"test_parseargs"
	};

	EXPECT_EQ(AmatsukazeCLI(sizeof(argv) / sizeof(argv[0]), argv), 0);
	
	argv[2] = L"0x6308";
	EXPECT_EQ(AmatsukazeCLI(sizeof(argv) / sizeof(argv[0]), argv), 0);
	
	argv[1] = L"--ourput";
	EXPECT_ANY_THROW(AmatsukazeCLI(sizeof(argv) / sizeof(argv[0]), argv));
}

TEST_F(TestBase, DecodePerformance)
{
	std::wstring srcDir = TestDataDir + L"\\";

	std::wstring srcfile = srcDir + LargeTsFile + L".ts";

	const wchar_t* args[] = {
		L"AmatsukazeTest.exe", L"--mode", L"test_perf",
		L"-i", srcfile.c_str(),
	};
	EXPECT_EQ(AmatsukazeCLI(LEN(args), args), 0);
}

void my_purecall_handler() {
	printf("It's pure virtual call !!!\n");
}

int main(int argc, char **argv)
{
	// エラーハンドラをセット
	_set_purecall_handler(my_purecall_handler);

	::testing::GTEST_FLAG(filter) = "*DecodePerformance*";
	::testing::InitGoogleTest(&argc, argv);
	int result = RUN_ALL_TESTS();

	getchar();

	return result;
}

