/**
* Amtasukaze Test Implementation
* Copyright (c) 2017-2018 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include "TranscodeManager.hpp"
#include "LogoScan.hpp"

namespace test {

static int PrintCRCTable(AMTContext& ctx, const ConfigWrapper& setting)
{
	CRC32 crc;

	const uint32_t* table = crc.getTable();

	for (int i = 0; i < 256; ++i) {
		fprintf(stderr, "0x%08x%c", table[i], ((i + 1) % 8) ? ',' : '\n');
	}

	return 0;
}

static int CheckCRC(AMTContext& ctx, const ConfigWrapper& setting)
{
	CRC32 crc;

	auto toBytesBE = [](uint8_t* bytes, uint32_t data) {
		bytes[3] = ((data >> 0) & 0xFF);
		bytes[2] = ((data >> 8) & 0xFF);
		bytes[1] = ((data >> 16) & 0xFF);
		bytes[0] = ((data >> 24) & 0xFF);
	};

	const uint8_t* data = (const uint8_t*)"ABCD";
	uint32_t result = crc.calc(data, 4, 123456);
	// fprintf(stderr, "RESULT: 0x%x\n");
	uint8_t buf[4]; toBytesBE(buf, result);
	uint32_t t = crc.calc(data, 4, 123456);
	result = crc.calc(buf, 4, t);

	if (result != 0) {
		fprintf(stderr, "[CheckCRC] Result does not match: 0x%x\n", result);
		return 1;
	}

	return 0;
}

static int ReadBits(AMTContext& ctx, const ConfigWrapper& setting)
{
	uint8_t data[16];
	srand(0);
	for (int i = 0; i < sizeof(data); ++i) data[i] = rand();

	//uint16_t a = read16(data);
	//uint32_t b = read24(data);
	//uint32_t c = read32(data);
	//uint64_t d = read40(data);
	uint64_t e = read48(data);

	fprintf(stderr, "sum=%f\n", double(e));

	return 0;
}

static int CheckAutoBuffer(AMTContext& ctx, const ConfigWrapper& setting)
{
	srand(0);

	std::unique_ptr<uint8_t[]> buf = std::unique_ptr<uint8_t[]>(new uint8_t[65536]);
	int addCnt = 0;
	int delCnt = 0;

	AutoBuffer ab;
	for (int i = 0; i < 10000; ++i) {
		int addNum = rand();
		int delNum = rand();

		for (int c = 0; c < addNum; ++c) {
			buf[c] = addCnt++;
		}
		//fprintf(stderr, "Add %d\n", addNum);
		ab.add(MemoryChunk(buf.get(), addNum));

		uint8_t *data = ab.ptr();
		for (int c = 0; c < (int)ab.size(); ++c) {
			if (data[c] != ((delCnt + c) & 0xFF)) {
				fprintf(stderr, "[CheckAutoBuffer] Result does not match\n");
				return 1;
			}
		}

		delNum = std::min<int>(delNum, (int)ab.size());
		//fprintf(stderr, "Del %d\n", delNum);
		ab.trimHead(delNum);
		delCnt += delNum;
	}

	return 0;
}

static int VerifyMpeg2Ps(AMTContext& ctx, const ConfigWrapper& setting) {
	enum {
		BUF_SIZE = 1400 * 1024 * 1024, // 1GB
	};
	auto buf = std::unique_ptr<uint8_t>(new uint8_t[BUF_SIZE]);
	FILE* fp = nullptr;
	if (fopen_s(&fp, setting.getSrcFilePath().c_str(), "rb")) {
		return 1;
	}
	try {
		AMTContext ctx;
		PsStreamVerifier psVerifier(ctx);

		size_t readBytes = fread(buf.get(), 1, BUF_SIZE, fp);
		psVerifier.verify(MemoryChunk(buf.get(), readBytes));
	}
	catch (const Exception& e) {
		fprintf(stderr, "Verify MPEG2-PS Error: 例外がスローされました -> %s\n", e.message());
		return 1;
	}
	fclose(fp);
	fp = NULL;

	return 0;
}

static int ReadTS(AMTContext& ctx, const ConfigWrapper& setting)
{
	try {
		auto splitter = std::unique_ptr<AMTSplitter>(new AMTSplitter(ctx, setting));
		if (setting.getServiceId() > 0) {
			splitter->setServiceId(setting.getServiceId());
		}
		StreamReformInfo reformInfo = splitter->split();
		reformInfo.serialize(setting.getStreamInfoPath());
	}
	catch (const Exception& e) {
		fprintf(stderr, "ReadTS Error: 例外がスローされました -> %s\n", e.message());
		return 1;
	}

	return 0;
}

static int AacDecode(AMTContext& ctx, const ConfigWrapper& setting)
{
	std::string srcfile = setting.getSrcFilePath() + ".aac";
	std::string testfile = setting.getSrcFilePath() + ".wav";

	FILE* fp = nullptr;
	if (fopen_s(&fp, srcfile.c_str(), "rb")) {
		return 1;
	}

	enum {
		BUF_SIZE = 1024 * 1024, // 1MB
	};
	auto buf = std::unique_ptr<uint8_t>(new uint8_t[BUF_SIZE]);
	size_t readBytes = fread(buf.get(), 1, BUF_SIZE, fp);

	AutoBuffer decoded;

	NeAACDecHandle hAacDec = NeAACDecOpen();
	/*
	NeAACDecConfigurationPtr conf = NeAACDecGetCurrentConfiguration(hAacDec);
	conf->outputFormat = FAAD_FMT_16BIT;
	NeAACDecSetConfiguration(hAacDec, conf);
	*/

	unsigned long samplerate;
	unsigned char channels;
	if (NeAACDecInit(hAacDec, buf.get(), (unsigned long)readBytes, &samplerate, &channels)) {
		fprintf(stderr, "NeAACDecInit failed\n");
		return 1;
	}

	printf("samplerate=%d, channels=%d\n", samplerate, channels);

	for (int i = 0; i < (int)readBytes; ) {
		NeAACDecFrameInfo frameInfo;
		void* samples = NeAACDecDecode(hAacDec, &frameInfo, buf.get() + i, (unsigned long)readBytes - i);
		decoded.add(MemoryChunk((uint8_t*)samples, frameInfo.samples * 2));
		i += frameInfo.bytesconsumed;
	}

	// 正解データと比較
	FILE* testfp = nullptr;
	if (fopen_s(&testfp, testfile.c_str(), "rb")) {
		return 1;
	}
	
	auto testbuf = std::unique_ptr<uint8_t>(new uint8_t[BUF_SIZE]);
	size_t testBytes = fread(testbuf.get(), 1, BUF_SIZE, testfp);
	// data chunkを探す
	for (int i = sizeof(RiffHeader); ; ) {
		if (!(i < (int)testBytes - 8)) {
			fprintf(stderr, "出力が小さすぎます\n");
			return 1;
		}
		if (read32(testbuf.get() + i) == 'data') {
			int testLength = (int)testBytes - i - 8;
			const uint16_t* pTest = (const uint16_t*)(testbuf.get() + i + 8);
			const uint16_t* pDec = (const uint16_t*)decoded.ptr();
			if (testLength != decoded.size()) {
				fprintf(stderr, "結果のサイズが合いません\n");
				return 1;
			}
			// AACのデコード結果は小数なので丸め誤差を考慮して
			for (int c = 0; c < testLength / 2; ++c) {
				if ((std::abs((int)pTest[c] - (int)pDec[c]) > 1)) {
					fprintf(stderr, "デコード結果が合いません\n");
					return 1;
				}
			}
			break;
		}
		i += *(uint32_t*)(testbuf.get() + i + 4) + 8;
	}

	NeAACDecClose(hAacDec);
	fclose(fp);
	fclose(testfp);

	return 0;
}

static int WaveWriteHeader(AMTContext& ctx, const ConfigWrapper& setting)
{
	std::string dstfile = setting.getOutFilePath(0, CMTYPE_BOTH);

	FILE* fp = nullptr;
	if(fopen_s(&fp, dstfile.c_str(), "wb")) {
		fprintf(stderr, "failed to open file...\n");
		return 1;
	}

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
	if (fwrite(samples, writeSeconds * sampleRate * nChannels, 1, fp) != 1) {
		fprintf(stderr, "failed to write file...\n");
		return 1;
	}

	free(samples);
	fclose(fp);

	return 0;
}

static int ProcessTest(AMTContext& ctx, const ConfigWrapper& setting)
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

	return 0;
}

static int FileStreamInfo(AMTContext& ctx, const ConfigWrapper& setting)
{
	StreamReformInfo reformInfo = StreamReformInfo::deserialize(ctx, setting.getStreamInfoPath());
	reformInfo.prepare(false);
	auto audioDiffInfo = reformInfo.genAudio();
	audioDiffInfo.printAudioPtsDiff(ctx);
	reformInfo.printOutputMapping([&](int index) { return setting.getOutFilePath(index, CMTYPE_BOTH); });
	return 0;
}

static int ParseArgs(AMTContext& ctx, const ConfigWrapper& setting)
{
	setting.dump();
	return 0;
}

static int LosslessTest(AMTContext& ctx, const ConfigWrapper& setting)
{
	auto env = make_unique_ptr(CreateScriptEnvironment2());
	auto codecEnc = make_unique_ptr(CCodec::CreateInstance(UTVF_ULH0, "Amatsukaze"));
	auto codecDec = make_unique_ptr(CCodec::CreateInstance(UTVF_ULH0, "Amatsukaze"));

	{
		PClip clip = env->Invoke("Import", setting.getFilterScriptPath().c_str()).AsClip();

		VideoInfo vi = clip->GetVideoInfo();

		size_t rawSize = vi.width * vi.height * 3 / 2;
		size_t outSize = codecEnc->EncodeGetOutputSize(UTVF_YV12, vi.width, vi.height);
		size_t headerSize = codecEnc->EncodeGetExtraDataSize();
		auto memIn = std::unique_ptr<uint8_t[]>(new uint8_t[rawSize]);
		auto memOut = std::unique_ptr<uint8_t[]>(new uint8_t[outSize]);
		auto memDec = std::unique_ptr<uint8_t[]>(new uint8_t[rawSize]);
		auto memHeader = std::unique_ptr<uint8_t[]>(new uint8_t[headerSize]);

		if (codecEnc->EncodeGetExtraData(memHeader.get(), headerSize, UTVF_YV12, vi.width, vi.height)) {
			THROW(RuntimeException, "failed to EncodeGetExtraData (UtVideo)");
		}
		if (codecEnc->EncodeBegin(UTVF_YV12, vi.width, vi.height, CBGROSSWIDTH_WINDOWS)) {
			THROW(RuntimeException, "failed to EncodeBegin (UtVideo)");
		}
		if (codecDec->DecodeBegin(UTVF_YV12, vi.width, vi.height, CBGROSSWIDTH_WINDOWS, memHeader.get(), headerSize)) {
			THROW(RuntimeException, "failed to DecodeBegin (UtVideo)");
		}

		size_t totalRawSize = 0;
		size_t totalCompSize = 0;

		for (int i = 0; i < 100; ++i) {
			PVideoFrame frame = clip->GetFrame(i + 100, env.get());
			CopyYV12(memIn.get(), frame, vi.width, vi.height);

			bool keyFrame = false;
			size_t outSize = codecEnc->EncodeFrame(memOut.get(), &keyFrame, memIn.get());
			codecDec->DecodeFrame(memDec.get(), memOut.get());

			if (memcmp(memIn.get(), memDec.get(), rawSize)) {
				printf("UtVideo Encode/Decode Test Error\n");
			}

			totalRawSize += rawSize;
			totalCompSize += outSize;
		}

		codecEnc->EncodeEnd();
		codecDec->DecodeEnd();

		float ratio = (float)totalCompSize / (float)totalRawSize;
		printf("Compression ratio: %.1f%%\n", ratio * 100.0f);
	}

	return 0;
}

static int LosslessFileTest(AMTContext& ctx, const ConfigWrapper& setting)
{
	auto env = make_unique_ptr(CreateScriptEnvironment2());
	auto codec = make_unique_ptr(CCodec::CreateInstance(UTVF_ULH0, "Amatsukaze"));

	{
		int numframes = 100;
		LosslessVideoFile file(ctx, setting.getOutFilePath(0, CMTYPE_BOTH), "wb");
		PClip clip = env->Invoke("Import", setting.getFilterScriptPath().c_str()).AsClip();

		VideoInfo vi = clip->GetVideoInfo();

		size_t rawSize = vi.width * vi.height * 3 / 2;
		size_t outSize = codec->EncodeGetOutputSize(UTVF_YV12, vi.width, vi.height);
		size_t extraSize = codec->EncodeGetExtraDataSize();
		auto memIn = std::unique_ptr<uint8_t[]>(new uint8_t[rawSize]);
		auto memOut = std::unique_ptr<uint8_t[]>(new uint8_t[outSize]);
		auto memDec = std::unique_ptr<uint8_t[]>(new uint8_t[rawSize]);
		std::vector<uint8_t> extra(extraSize);

		if (codec->EncodeGetExtraData(extra.data(), extraSize, UTVF_YV12, vi.width, vi.height)) {
			THROW(RuntimeException, "failed to EncodeGetExtraData (UtVideo)");
		}
		if (codec->EncodeBegin(UTVF_YV12, vi.width, vi.height, CBGROSSWIDTH_WINDOWS)) {
			THROW(RuntimeException, "failed to EncodeBegin (UtVideo)");
		}
		file.writeHeader(vi.width, vi.height, numframes, extra);

		for (int i = 0; i < numframes; ++i) {
			PVideoFrame frame = clip->GetFrame(i + 100, env.get());
			CopyYV12(memIn.get(), frame, vi.width, vi.height);

			bool keyFrame = false;
			size_t codedSize = codec->EncodeFrame(memOut.get(), &keyFrame, memIn.get());
			file.writeFrame(memOut.get(), (int)codedSize);
		}
		codec->EncodeEnd();
	}

	{
		LosslessVideoFile file(ctx, setting.getOutFilePath(0, CMTYPE_BOTH), "rb");
		file.readHeader();

		int width = file.getWidth();
		int height = file.getHeight();
		int numframes = file.getNumFrames();
		auto extra = file.getExtra();

		size_t rawSize = width * height * 3 / 2;
		size_t outSize = codec->EncodeGetOutputSize(UTVF_YV12, width, height);
		auto memDec = std::unique_ptr<uint8_t[]>(new uint8_t[rawSize]);
		auto memOut = std::unique_ptr<uint8_t[]>(new uint8_t[outSize]);

		if (codec->DecodeBegin(UTVF_YV12, width, height, CBGROSSWIDTH_WINDOWS, extra.data(), (int)extra.size())) {
			THROW(RuntimeException, "failed to DecodeBegin (UtVideo)");
		}

		for (int i = 0; i < numframes; ++i) {
			int64_t codedSize = file.readFrame(i, memOut.get());
			if (codec->DecodeFrame(memDec.get(), memOut.get()) != rawSize) {
				THROW(RuntimeException, "failed to DecodeFrame (UtVideo)");
			}
		}

		codec->DecodeEnd();
	}

	return 0;
}

static int LogoFrameTest(AMTContext& ctx, const ConfigWrapper& setting)
{
	{
		auto env = make_unique_ptr(CreateScriptEnvironment2());
		PClip clip = env->Invoke("Import", setting.getFilterScriptPath().c_str()).AsClip();

		logo::LogoFrame logof(ctx, setting.getLogoPath(), 0.1f);
		logof.scanFrames(clip, env.get());
		logof.writeResult(setting.getTmpLogoFramePath(0));

		printf("BestLogo: %s\n", setting.getLogoPath()[logof.getBestLogo()].c_str());
		printf("LogoRatio: %f\n", logof.getLogoRatio());
	}

	return 0;
}

class TestSplitDualMono : public DualMonoSplitter
{
	std::unique_ptr<File> file0;
	std::unique_ptr<File> file1;
public:
	TestSplitDualMono(AMTContext& ctx, const std::vector<std::string>& outpaths)
		: DualMonoSplitter(ctx)
		, file0(new File(outpaths[0].c_str(), "wb"))
		, file1(new File(outpaths[1].c_str(), "wb"))
	{ }

	virtual void OnOutFrame(int index, MemoryChunk mc)
	{
		((index == 0) ? file0.get() : file1.get())->write(mc);
	}
};

static int SplitDualMonoAAC(AMTContext& ctx, const ConfigWrapper& setting)
{
	std::vector<std::string> outpaths;
	outpaths.push_back(setting.getIntAudioFilePath(0, 0, 0, CMTYPE_BOTH));
	outpaths.push_back(setting.getIntAudioFilePath(0, 0, 1, CMTYPE_BOTH));
	TestSplitDualMono splitter(ctx, outpaths);

	File src(setting.getSrcFilePath(), "rb");
	int sz = (int)src.size();
	std::unique_ptr<uint8_t[]> buf = std::unique_ptr<uint8_t[]>(new uint8_t[sz]);
	src.read(MemoryChunk(buf.get(), sz));

	for (int offset = 0; offset + 7 <= sz; ) {
		AdtsHeader header;
		if (!header.parse(buf.get() + offset, 7)) {
			THROW(FormatException, "Failed to parse AAC frame ...");
		}
		if (offset + header.frame_length > sz) {
			THROW(FormatException, "frame_length too long ...");
		}
		splitter.inputPacket(MemoryChunk(buf.get() + offset, header.frame_length));
		offset += header.frame_length;
	}

	return 0;
}

static int AACDecodeTest(AMTContext& ctx, const ConfigWrapper& setting)
{
	File src(setting.getSrcFilePath(), "rb");
	int sz = (int)src.size();
	std::unique_ptr<uint8_t[]> buf = std::unique_ptr<uint8_t[]>(new uint8_t[sz]);
	src.read(MemoryChunk(buf.get(), sz));

	NeAACDecHandle hAacDec = NeAACDecOpen();
	NeAACDecConfigurationPtr conf = NeAACDecGetCurrentConfiguration(hAacDec);
	conf->outputFormat = FAAD_FMT_16BIT;
	NeAACDecSetConfiguration(hAacDec, conf);


	for (int offset = 0; offset + 7 <= sz; ) {
		AdtsHeader header;
		if (!header.parse(buf.get() + offset, 7)) {
			THROW(FormatException, "Failed to parse AAC frame ...");
		}
		if (offset + header.frame_length > sz) {
			THROW(FormatException, "frame_length too long ...");
		}
		if (offset == 0) {
			unsigned long samplerate;
			unsigned char channels;
			if (NeAACDecInit(hAacDec, buf.get() + offset, (int)header.frame_length, &samplerate, &channels)) {
				ctx.warn("NeAACDecInitに失敗");
				return 1;
			}
		}
		NeAACDecFrameInfo frameInfo;
		void* samples = NeAACDecDecode(hAacDec, &frameInfo, buf.get() + offset, header.frame_length);
		if (frameInfo.error != 0) {
			THROW(FormatException, "デコード失敗 ...");
		}
		offset += header.frame_length;
	}

	return 0;
}

static int CaptionASS(AMTContext& ctx, const ConfigWrapper& setting)
{
	try {
		StreamReformInfo reformInfo = StreamReformInfo::deserialize(ctx, setting.getStreamInfoPath());
		
		reformInfo.prepare(false);
		auto audioDiffInfo = reformInfo.genAudio();
		audioDiffInfo.printAudioPtsDiff(ctx);

		CaptionASSFormatter formatterASS(ctx);
		CaptionSRTFormatter formatterSRT(ctx);
		int numFiles = reformInfo.getNumVideoFile();
		for (int videoFileIndex = 0; videoFileIndex < numFiles; ++videoFileIndex) {
			int numEncoders = reformInfo.getNumEncoders(videoFileIndex);
			for (int encoderIndex = 0; encoderIndex < numEncoders; ++encoderIndex) {
				for (int c = 0; c < CMTYPE_MAX; ++c) {
					auto& capList = reformInfo.getOutCaptionList(encoderIndex, videoFileIndex, (CMType)c);
					for (int lang = 0; lang < capList.size(); ++lang) {
						WriteUTF8File(
							setting.getTmpASSFilePath(videoFileIndex, encoderIndex, lang, (CMType)c),
							formatterASS.generate(capList[lang]));
						WriteUTF8File(
							setting.getTmpSRTFilePath(videoFileIndex, encoderIndex, lang, (CMType)c),
							formatterSRT.generate(capList[lang]));
					}
				}
			}
		}
	}
	catch (const Exception& e) {
		fprintf(stderr, "CaptionASS Error: 例外がスローされました -> %s\n", e.message());
		return 1;
	}

	return 0;
}

static int EncoderOptionParse(AMTContext& ctx, const ConfigWrapper& setting)
{
	auto info = ParseEncoderOption(ENCODER_NVENC, setting.getEncoderOptions());
	PrintEncoderInfo(ctx, info);
	return 0;
}

static int DecodePerformance(AMTContext& ctx, const ConfigWrapper& setting)
{
	using namespace av;

	InputContext inputCtx(setting.getSrcFilePath());
	if (avformat_find_stream_info(inputCtx(), NULL) < 0) {
		THROW(FormatException, "avformat_find_stream_info failed");
	}
	AVStream *videoStream = av::GetVideoStream(inputCtx());
	if (videoStream == NULL) {
		THROW(FormatException, "Could not find video stream ...");
	}
	AVCodecID vcodecId = videoStream->codecpar->codec_id;
	AVCodec *pCodec = avcodec_find_decoder(vcodecId);
	if (pCodec == NULL) {
		THROW(FormatException, "Could not find decoder ...");
	}
	CodecContext codecCtx(pCodec);
	if (avcodec_parameters_to_context(codecCtx(), videoStream->codecpar) != 0) {
		THROW(FormatException, "avcodec_parameters_to_context failed");
	}
	codecCtx()->thread_count = GetFFmpegThreads(GetProcessorCount() - 2);
	if (avcodec_open2(codecCtx(), pCodec, NULL) != 0) {
		THROW(FormatException, "avcodec_open2 failed");
	}

	Stopwatch sw;
	sw.start();

	int nframes = 0;
	Frame frame;
	AVPacket packet = AVPacket();
	while (av_read_frame(inputCtx(), &packet) == 0) {
		if (packet.stream_index == videoStream->index) {
			if (avcodec_send_packet(codecCtx(), &packet) != 0) {
				THROW(FormatException, "avcodec_send_packet failed");
			}
			while (avcodec_receive_frame(codecCtx(), frame()) == 0) {
				++nframes;
			}
		}
		av_packet_unref(&packet);
	}

	// flush decoder
	if (avcodec_send_packet(codecCtx(), NULL) != 0) {
		THROW(FormatException, "avcodec_send_packet failed");
	}
	while (avcodec_receive_frame(codecCtx(), frame()) == 0) {
		++nframes;
	}

	sw.stop();
	double sec = sw.getTotal();
	printf("%f sec for %d frames ... %f fps\n", sec, nframes, nframes / sec);

	return 0;
}

static int BitrateZones(AMTContext& ctx, const ConfigWrapper& setting)
{
  std::vector<int> durations;
  for (int i = 0; i < 30; ++i) {
    durations.push_back(2);
    durations.push_back(3);
  }
  for (int i = 0; i < 40; ++i) {
    durations.push_back(1);
  }
  for (int i = 0; i < 50; ++i) {
    durations.push_back(2);
  }
  std::vector<EncoderZone> cmzones;
  cmzones.push_back(EncoderZone{ 40, 80 });
  cmzones.push_back(EncoderZone{ 110, 130 });

  auto ret = MakeVFRBitrateZones(durations, cmzones, 0.6, 60000, 1001, 1.0, 0.15);

  if (ret.size() != 3) THROW(TestException, "");
  if (ret[0].startFrame != 0) THROW(TestException, "");
  if (ret[0].endFrame != 40) THROW(TestException, "");
  if (ret[0].bitrate != 2.5) THROW(TestException, "");
  if (ret[1].startFrame != 40) THROW(TestException, "");
  if (ret[1].endFrame != 128) THROW(TestException, "");
  if (std::abs(1.195 - ret[1].bitrate) > 0.01) THROW(TestException, "");
  if (ret[2].startFrame != 128) THROW(TestException, "");
  if (ret[2].endFrame != 150) THROW(TestException, "");
  if (ret[2].bitrate != 2.0) THROW(TestException, "");

  return 0;
}

static int BitrateZonesBug(AMTContext& ctx, const ConfigWrapper& setting)
{
	File dump(setting.getSrcFilePath(), "rb");
	auto durations = dump.readArray<int>();
	auto cmzones = dump.readArray<EncoderZone>();
	auto bitrateCM = dump.readValue<double>();
	auto fpsNum = dump.readValue<int>();
	auto fpsDenom = dump.readValue<int>();
	auto timeFactor = dump.readValue<double>();
	auto costLimit = dump.readValue<double>();

	auto ret = MakeVFRBitrateZones(durations, cmzones, bitrateCM, fpsNum, fpsDenom, timeFactor, costLimit);

	return 0;
}

static int PrintfBug(AMTContext& ctx, const ConfigWrapper& setting)
{
	File txtf(setting.getSrcFilePath(), "rb");
	std::vector<char> strv(txtf.size());
	txtf.read(MemoryChunk((uint8_t*)strv.data(), strv.size()));
	printf("txt len: %d\n", (int)strv.size());
	std::string str(strv.begin(), strv.end());
	ctx.info(str.c_str());
	return 0;
}

} // namespace test
