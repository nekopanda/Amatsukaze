// TsSplitterUnitTest.cpp : コンソール アプリケーションのエントリ ポイントを定義します。
//
#define _CRT_SECURE_NO_WARNINGS

#include <string>
#include <cmath>

#include "gtest/gtest.h"

#include "TsSplitter.hpp"
#include "ProcessThread.hpp"
#include "Transcode.hpp"
#include "TranscodeManager.hpp"
#include "WaveWriter.h"

// FAAD
#include "faad.h"
#pragma comment(lib, "libfaad2.lib")

static bool fileExists(const char* filepath) {
	WIN32_FIND_DATAA findData;
	HANDLE hFind = FindFirstFileA(filepath, &findData);
	if (hFind == INVALID_HANDLE_VALUE) {
		return false;
	}
	FindClose(hFind);
	return true;
}

// テスト対象となるクラス Foo のためのフィクスチャ
class TestBase : public ::testing::Test {
protected:

	TestBase() {
		char curdir[200];
		GetCurrentDirectoryA(sizeof(curdir), curdir);
		std::string inipath = curdir;
		inipath += "\\TestParam.ini";

		getParam(inipath, "TestDataDir", TestDataDir);
		getParam(inipath, "TestWorkDir", TestWorkDir);
		getParam(inipath, "MPEG2VideoTsFile", MPEG2VideoTsFile);
		getParam(inipath, "H264VideoTsFile", H264VideoTsFile);
		getParam(inipath, "OneSegVideoTsFile", OneSegVideoTsFile);
		getParam(inipath, "SampleAACFile", SampleAACFile);
		getParam(inipath, "SampleMPEG2PsFile", SampleMPEG2PsFile);
		getParam(inipath, "VideoFormatChangeTsFile", VideoFormatChangeTsFile);
    getParam(inipath, "AudioFormatChangeTsFile", AudioFormatChangeTsFile);
    getParam(inipath, "RffFieldPictureTsFile", RffFieldPictureTsFile);
    getParam(inipath, "BFFMPEG2VideoTsFile", BFFMPEG2VideoTsFile);
    getParam(inipath, "DropTsFile", DropTsFile);
    getParam(inipath, "VideoDropTsFile", VideoDropTsFile);
    getParam(inipath, "AudioDropTsFile", AudioDropTsFile);
    getParam(inipath, "PullDownTsFile", PullDownTsFile);
		getParam(inipath, "LargeTsFile", LargeTsFile);
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
	std::string TestDataDir;
	std::string TestWorkDir;
	std::string MPEG2VideoTsFile;
	std::string H264VideoTsFile;
	std::string OneSegVideoTsFile;
	std::string SampleAACFile;
	std::string SampleMPEG2PsFile;
	std::string VideoFormatChangeTsFile;
  std::string AudioFormatChangeTsFile;
  std::string RffFieldPictureTsFile;
  std::string BFFMPEG2VideoTsFile;
  std::string DropTsFile;
  std::string VideoDropTsFile;
  std::string AudioDropTsFile;
  std::string PullDownTsFile;
	std::string LargeTsFile;

	void ParserTest(const std::string& filename, bool verify = true);

private:
	void getParam(const std::string& inipath, const char* key, std::string& dst) {
		char buf[200];
		if (GetPrivateProfileStringA("FILE", key, "", buf, sizeof(buf), inipath.c_str())) {
			dst = buf;
		}
	}
};

TEST(CRC, PrintTable) {

	CRC32 crc;

	const uint32_t* table = crc.getTable();

	for (int i = 0; i < 256; ++i) {
		printf("0x%08x%c", table[i], ((i+1) % 8) ? ',' : '\n');
	}
}

static void toBytesBE(uint8_t* bytes, uint32_t data) {
	bytes[3] = ((data >> 0) & 0xFF);
	bytes[2] = ((data >> 8) & 0xFF);
	bytes[1] = ((data >> 16) & 0xFF);
	bytes[0] = ((data >> 24) & 0xFF);
}

TEST(CRC, CheckCRC) {

	CRC32 crc;

	const uint8_t* data = (const uint8_t*)"ABCD";
	uint32_t result = crc.calc(data, 4, 123456);
	// printf("RESULT: 0x%x\n");
	uint8_t buf[4]; toBytesBE(buf, result);
	uint32_t t = crc.calc(data, 4, 123456);
	result = crc.calc(buf, 4, t);
	EXPECT_EQ(result, 0);
}

TEST(Util, readOpt) {
	uint8_t data[16];
	srand(0);
	for (int i = 0; i < sizeof(data); ++i) data[i] = rand();

	//uint16_t a = read16(data);
	//uint32_t b = read24(data);
	//uint32_t c = read32(data);
	//uint64_t d = read40(data);
	uint64_t e = read48(data);

	printf("sum=%f\n", double(e));
}

TEST(Util, AutoBufferTest) {

  srand(0);

  uint8_t *buf = new uint8_t[65536];
  int addCnt = 0;
  int delCnt = 0;

  AutoBuffer ab;
  for (int i = 0; i < 10000; ++i) {
    int addNum = rand();
    int delNum = rand();

    for (int c = 0; c < addNum; ++c) {
      buf[c] = addCnt++;
    }
    //printf("Add %d\n", addNum);
    ab.add(buf, addNum);

    uint8_t *data = ab.get();
    for (int c = 0; c < ab.size(); ++c) {
      if (data[c] != ((delCnt + c) & 0xFF)) {
        ASSERT_TRUE(false);
      }
    }

    delNum = std::min<int>(delNum, (int)ab.size());
    //printf("Del %d\n", delNum);
    ab.trimHead(delNum);
    delCnt += delNum;
  }
  delete buf;
}

void VerifyMpeg2Ps(std::string srcfile) {
	enum {
		BUF_SIZE = 1400 * 1024 * 1024, // 1GB
	};
	uint8_t* buf = (uint8_t*)malloc(BUF_SIZE); // 
	FILE* fp = fopen(srcfile.c_str(), "rb");
	try {
		AMTContext ctx;
		PsStreamVerifier psVerifier(ctx);

		size_t readBytes = fread(buf, 1, BUF_SIZE, fp);
		psVerifier.verify(MemoryChunk(buf, readBytes));
	}
	catch (Exception e) {
		printf("Verify MPEG2-PS Error: 例外がスローされました -> %s\n", e.message());
		ADD_FAILURE();
	}
	free(buf);
	buf = NULL;
	fclose(fp);
	fp = NULL;
}

void TestBase::ParserTest(const std::string& filename, bool verify) {
	std::string srcDir = TestDataDir + "\\";
	std::string dstDir = TestWorkDir + "\\";

	std::string srcfile = srcDir + filename + ".ts";
	std::string mpgfile = dstDir + filename + ".mpg";
	std::string aacfile = dstDir + filename + ".aac";
	std::string wavfile = dstDir + filename + ".wav";

	if (filename.size() == 0 || !fileExists(srcfile.c_str())) {
		printf("テストファイルがないのでスキップ: %s\n", srcfile.c_str());
		return;
	}

	FILE* srcfp = fopen(srcfile.c_str(), "rb");
	FILE* mpgfp = fopen(mpgfile.c_str(), "wb");
	FILE* aacfp = fopen(aacfile.c_str(), "wb");
	FILE* wavfp = fopen(wavfile.c_str(), "wb");
	ASSERT_TRUE(srcfp != NULL);
	ASSERT_TRUE(mpgfp != NULL);
	ASSERT_TRUE(aacfp != NULL);
	ASSERT_TRUE(wavfp != NULL);

  /* TODO: TsSplitterテストを作る
	AMTContext ctx;
	TsSplitter tsSplitter(&ctx);
	tsSplitter.mpgfp = mpgfp;
	tsSplitter.aacfp = aacfp;
	tsSplitter.wavfp = wavfp;

	int bufsize = 16 * 1024;
	uint8_t* buffer = (uint8_t*)malloc(bufsize);

	while (1) {
		int readsize = (int)fread(buffer, 1, bufsize, srcfp);
		if (readsize == 0) break;
		tsSplitter.inputTsData(MemoryChunk(buffer, readsize));
	}

	tsSplitter.flush();
	tsSplitter.printInteraceCount();

	free(buffer);
	fclose(srcfp);
	fclose(mpgfp);
	fclose(aacfp);
	fclose(wavfp);
  */

	// 出力ファイルをチェック
	if (verify) {
		VerifyMpeg2Ps(mpgfile);
	}
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
	std::string srcfile = TestDataDir + "\\" + SampleMPEG2PsFile + ".mpg";
	VerifyMpeg2Ps(srcfile);
}

// FAADデコードが正しい出力をするかテスト
TEST_F(TestBase, AacDecodeVerifyTest) {
	std::string srcDir = TestDataDir + "\\";
	std::string filename = SampleAACFile;
	std::string srcfile = srcDir + filename + ".aac";
	std::string testfile = srcDir + filename + ".wav";

	if (filename.size() == 0 || !fileExists(srcfile.c_str())) {
		printf("テストファイルがないのでスキップ: %s\n", srcfile.c_str());
		return;
	}
	if (!fileExists(testfile.c_str())) {
		printf("テストファイルがないのでスキップ: %s\n", testfile.c_str());
		return;
	}

	FILE* fp = fopen(srcfile.c_str(), "rb");

	enum {
		BUF_SIZE = 1024 * 1024, // 1MB
	};
	uint8_t* buf = (uint8_t*)malloc(BUF_SIZE);
	size_t readBytes = fread(buf, 1, BUF_SIZE, fp);

	AutoBuffer decoded;

	NeAACDecHandle hAacDec = NeAACDecOpen();
	/*
	NeAACDecConfigurationPtr conf = NeAACDecGetCurrentConfiguration(hAacDec);
	conf->outputFormat = FAAD_FMT_16BIT;
	NeAACDecSetConfiguration(hAacDec, conf);
	*/

	unsigned long samplerate;
	unsigned char channels;
	ASSERT_FALSE(NeAACDecInit(hAacDec, buf, (unsigned long)readBytes, &samplerate, &channels));

	printf("samplerate=%d, channels=%d\n", samplerate, channels);

	for (int i = 0; i < readBytes; ) {
		NeAACDecFrameInfo frameInfo;
		void* samples = NeAACDecDecode(hAacDec, &frameInfo, buf + i, (unsigned long)readBytes - i);
		decoded.add((uint8_t*)samples, frameInfo.samples * 2);
		i += frameInfo.bytesconsumed;
	}

	// 正解データと比較
	FILE* testfp = fopen(testfile.c_str(), "rb");
	uint8_t* testbuf = (uint8_t*)malloc(BUF_SIZE);
	size_t testBytes = fread(testbuf, 1, BUF_SIZE, testfp);
	// data chunkを探す
	for (int i = sizeof(RiffHeader); ; ) {
		ASSERT_TRUE(i < testBytes - 8);
		if (read32(testbuf + i) == 'data') {
			int testLength = (int)testBytes - i - 8;
			const uint16_t* pTest = (const uint16_t*)(testbuf + i + 8);
			const uint16_t* pDec = (const uint16_t*)decoded.get();
			EXPECT_TRUE(testLength == decoded.size());
			// AACのデコード結果は小数なので丸め誤差を考慮して
			for (int c = 0; c < testLength/2; ++c) {
				EXPECT_TRUE(std::abs((int)pTest[c] -  (int)pDec[c]) <= 1);
			}
			break;
		}
		i += *(uint32_t*)(testbuf + i + 4) + 8;
	}

	NeAACDecClose(hAacDec);
	free(buf);
	free(testbuf);
	fclose(fp);
	fclose(testfp);
}

TEST_F(TestBase, WaveWriter) {
	std::string dstDir = TestWorkDir + "\\";
	std::string dstfile = dstDir + "fake.wav";

	FILE* fp = fopen(dstfile.c_str(), "wb");
	ASSERT_TRUE(fp != NULL);

	int writeSeconds = 300;
	int sampleRate = 24000;
	int bitsPerSample = 8;
	int nChannels = 1;

	uint8_t* samples = (uint8_t*)malloc(writeSeconds * sampleRate * nChannels * (bitsPerSample / 2));
	for (int i = 0; i < writeSeconds * sampleRate; ++i) {
		for (int c = 0; c < nChannels; ++c) {
			samples[i * nChannels + c] = (i % sampleRate);
		}
	}

	writeWaveHeader(fp, nChannels, sampleRate, bitsPerSample, writeSeconds * sampleRate);
	ASSERT_TRUE(fwrite(samples, writeSeconds * sampleRate * nChannels, 1, fp) == 1);

	free(samples);
	fclose(fp);
}


// swscale
#pragma comment(lib, "swscale.lib")

// OpenCV
#include <opencv2/imgcodecs.hpp>

#ifndef _DEBUG
#pragma comment(lib, "opencv_world320.lib")
#else
#pragma comment(lib, "opencv_world320d.lib")
#endif

#include <direct.h>

class ImageWriter
{
public:
  ImageWriter(const std::string& path, AVPixelFormat src_fmt, int width, int height)
  {
    this->dirPath = path;
    this->src_fmt = src_fmt;
    this->width = width;
    this->height = height;

    _mkdir(path.c_str());

    sws_ctx = sws_getContext(width, height, src_fmt, width, height,
      AV_PIX_FMT_BGR24, SWS_BILINEAR, NULL, NULL, NULL);

    pFrameRGB = av::AllocPicture(AV_PIX_FMT_BGR24, width, height);
  }
  ~ImageWriter()
  {
    sws_freeContext(sws_ctx); sws_ctx = NULL;
    av::FreePicture(pFrameRGB);
  }

  void Write(AVFrame* pFrame)
  {
    
    ASSERT_TRUE(src_fmt == pFrame->format);
    ASSERT_TRUE(width == pFrame->width);
    ASSERT_TRUE(height == pFrame->height);

    sws_scale(sws_ctx, (uint8_t const * const *)pFrame->data,
      pFrame->linesize, 0, pFrame->height,
      pFrameRGB->data, pFrameRGB->linesize);

    cv::Mat img(pFrame->height, pFrame->width, CV_8UC3,
      pFrameRGB->data[0], pFrameRGB->linesize[0]);
    cv::imwrite(getNextFileName(), img);
  }

private:
  std::string dirPath;
  SwsContext *sws_ctx;
  AVFrame *pFrameRGB;

  AVPixelFormat src_fmt;
  int width;
  int height;

  int fileCnt = 0;
  char nameBuffer[256];

  const char* getNextFileName()
  {
    sprintf_s(nameBuffer, "%s\\frame-%06d.bmp", dirPath.c_str(), fileCnt++);
    return nameBuffer;
  }
};

PICTURE_TYPE avPictureType(int interalced, int tff, int repeat_pict)
{
  switch (repeat_pict) {
  case 0:
    return interalced ? (tff ? PIC_TFF : PIC_BFF) : PIC_FRAME;
  case 1:
    return tff ? PIC_TFF_RFF : PIC_BFF_RFF;
  case 2:
    return PIC_FRAME_DOUBLING;
  case 4:
    return PIC_FRAME_TRIPLING;
  default:
    THROWF(FormatException, "不明なrepeat_pict(%d)です", repeat_pict);
  }
  return PIC_FRAME; // 警告抑制
}

FRAME_TYPE avFrameType(AVPictureType type)
{
  switch (type) {
  case AV_PICTURE_TYPE_I:
    return FRAME_I;
  case AV_PICTURE_TYPE_P:
    return FRAME_P;
  case AV_PICTURE_TYPE_B:
    return FRAME_B;
  default:
    return FRAME_OTHER;
  }
}

TEST_F(TestBase, ffmpegEncode) {

  std::string srcFile = TestWorkDir + "\\" + MPEG2VideoTsFile + ".mpg";
  std::string outFile = TestWorkDir + "\\" + MPEG2VideoTsFile + ".264";

  std::string options = "--crf 23";

  VideoFormat fmt;
  fmt.width = 1440;
  fmt.height = 1080;
  fmt.sarWidth = 4;
  fmt.sarHeight = 3;
  fmt.frameRateNum = 30000;
  fmt.frameRateDenom = 1001;
  fmt.colorPrimaries = 1;
  fmt.transferCharacteristics = 1;
  fmt.colorSpace = 1;
  fmt.progressive = false;

  std::vector<VideoFormat> fmts = { fmt };
  std::vector<std::string> args = { makeEncoderArgs(ENCODER_X264, "x264.exe", options, fmt, outFile) };
  printf("args: %s\n", args[0].c_str());

  std::map<int64_t, int> dummy;

  av::MultiOutTranscoder trans;
  trans.encode(srcFile, fmts, args, dummy, fmt.width * fmt.height * 3);
}

TEST_F(TestBase, ffmpegTest) {

  std::string srcFile = TestWorkDir + "\\" + LargeTsFile + ".mpg";

  AVFormatContext *pFormatCtx = NULL;
  ASSERT_TRUE(avformat_open_input(&pFormatCtx, srcFile.c_str(), NULL, NULL) == 0);
  ASSERT_TRUE(avformat_find_stream_info(pFormatCtx, NULL) >= 0);
  av_dump_format(pFormatCtx, 0, srcFile.c_str(), 0);
  AVStream *videoStream = av::GetVideoStream(pFormatCtx);
  ASSERT_TRUE(videoStream != NULL);
  AVCodec *pCodec = avcodec_find_decoder(videoStream->codecpar->codec_id);
  ASSERT_TRUE(pCodec != NULL);
  AVCodecContext *pCodecCtx = avcodec_alloc_context3(pCodec);
  ASSERT_TRUE(avcodec_parameters_to_context(pCodecCtx, videoStream->codecpar) == 0);
  ASSERT_TRUE(avcodec_open2(pCodecCtx, pCodec, NULL) == 0);
  AVFrame *pFrame = av_frame_alloc();

  {
    //ImageWriter writer(TestWorkDir + "\\decoded", pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height);
    FILE* framesfp = fopen("ffmpeg_frames.txt", "w");
    fprintf(framesfp, "PTS,FRAME_TYPE,PIC_TYPE,PixelFormat,IsGOPStart\n");

    AVPacket packet;
    while (av_read_frame(pFormatCtx, &packet) == 0) {
      if (packet.stream_index == videoStream->index) {
        ASSERT_TRUE(avcodec_send_packet(pCodecCtx, &packet) == 0);
        while (avcodec_receive_frame(pCodecCtx, pFrame) == 0) {
          //writer.Write(pFrame);
          auto picType = avPictureType(pFrame->interlaced_frame, pFrame->top_field_first, pFrame->repeat_pict);
          auto frameType = avFrameType(pFrame->pict_type);
          fprintf(framesfp, "%lld,%s,%s,%d,%d\n",
            pFrame->pts, FrameTypeString(frameType), PictureTypeString(picType), pFrame->format, pFrame->key_frame);
        }
      }
      av_packet_unref(&packet);
    }

    fclose(framesfp);
  }
  
  av_frame_free(&pFrame);
  avcodec_free_context(&pCodecCtx);
  avformat_close_input(&pFormatCtx);
}

// Process/Thread Test

TEST(Process, SimpleProcessTest)
{
  class ProcTest : public EventBaseSubProcess {
  public:
    ProcTest() : EventBaseSubProcess("x264.exe --help") { }
  protected:
    virtual void onOut(bool isErr, MemoryChunk mc) {
      fwrite(mc.data, mc.length, 1, stdout);
    }
  };

  ProcTest proc;
  proc.join();
}

// Encode Test

TEST_F(TestBase, encodeMpeg2Test)
{
  std::string srcDir = TestDataDir + "\\";
  std::string dstDir = TestWorkDir + "\\";

  TranscoderSetting setting;
  setting.tsFilePath = srcDir + MPEG2VideoTsFile + ".ts";
  setting.outVideoPath = dstDir + "Mpeg2Test";
  setting.intFileBasePath = dstDir + "Mpeg2TestInt";
  setting.audioFilePath = dstDir + "Mpeg2TestAudio.dat";
  setting.encoder = ENCODER_X264;
  setting.encoderPath = "x264.exe";
  setting.encoderOptions = "--crf 23";
  setting.muxerPath = "muxer.exe";

  AMTContext ctx;
  transcodeMain(ctx, setting);
}

void my_purecall_handler() {
  printf("It's pure virtual call !!!\n");
}

int main(int argc, char **argv)
{
  // エラーハンドラをセット
  _set_purecall_handler(my_purecall_handler);

  // FFMPEGライブラリ初期化
  av_register_all();

	::testing::GTEST_FLAG(filter) = "*encodeMpeg2Test";
	::testing::InitGoogleTest(&argc, argv);
	int result = RUN_ALL_TESTS();

	getchar();

	return result;
}

