#pragma once

#include <stdint.h>

#include "ProcessThread.hpp"
#include "StreamReform.hpp"

namespace wave {

struct Header
{
	int8_t chunkID[4]; //"RIFF" = 0x46464952
	uint32_t chunkSize; //28 [+ sizeof(wExtraFormatBytes) + wExtraFormatBytes] + sum(sizeof(chunk.id) + sizeof(chunk.size) + chunk.size)
	int8_t format[4]; //"WAVE" = 0x45564157
	int8_t subchunk1ID[4]; //"fmt " = 0x20746D66
	uint32_t subchunk1Size; //16 [+ sizeof(wExtraFormatBytes) + wExtraFormatBytes]
	uint16_t audioFormat;
	uint16_t numChannels;
	uint32_t sampleRate;
	uint32_t byteRate;
	uint16_t blockAlign;
	uint16_t bitsPerSample;
	int8_t subchunk2ID[4];
	uint32_t subchunk2Size;
};

void set4(int8_t dst[4], const char* src) {
	dst[0] = src[0];
	dst[1] = src[1];
	dst[2] = src[2];
	dst[3] = src[3];
}

} // namespace wave {

void EncodeAudio(AMTContext& ctx, const tstring& encoder_args,
	const tstring& audiopath, const AudioFormat& afmt,
	const std::vector<FilterAudioFrame>& audioFrames)
{
	using namespace wave;

	ctx.info("[音声エンコーダ起動]");
	ctx.infoF("%s", encoder_args);

	auto process = std::unique_ptr<StdRedirectedSubProcess>(
		new StdRedirectedSubProcess(encoder_args, 5));

	int nchannels = 2;
	int bytesPerSample = 2;
	Header header;
	set4(header.chunkID, "RIFF");
	header.chunkSize = 0; // サイズ未定
	set4(header.format, "WAVE");
	set4(header.subchunk1ID, "fmt ");
	header.subchunk1Size = 16;
	header.audioFormat = 1;
	header.numChannels = nchannels;
	header.sampleRate = afmt.sampleRate;
	header.byteRate = afmt.sampleRate * bytesPerSample * nchannels;
	header.blockAlign = bytesPerSample * nchannels;
	header.bitsPerSample = bytesPerSample * 8;
	set4(header.subchunk2ID, "data");
	header.subchunk2Size = 0; // サイズ未定

	process->write(MemoryChunk((uint8_t*)&header, sizeof(header)));

	int audioSamplesPerFrame = 1024;
	// waveLengthはゼロのこともあるので注意
	for (int i = 0; i < (int)audioFrames.size(); ++i) {
		if (audioFrames[i].waveLength != 0) {
			audioSamplesPerFrame = audioFrames[i].waveLength / 4; // 16bitステレオ前提
			break;
		}
	}

	File srcFile(audiopath, _T("rb"));
	AutoBuffer buffer;
	int frameWaveLength = audioSamplesPerFrame * bytesPerSample * nchannels;
	MemoryChunk mc = buffer.space(frameWaveLength);
	mc.length = frameWaveLength;

	for (size_t i = 0; i < audioFrames.size(); ++i) {
		if (audioFrames[i].waveLength != 0) {
			// waveがあるなら読む
			srcFile.seek(audioFrames[i].waveOffset, SEEK_SET);
			srcFile.read(mc);
		}
		else {
			// ない場合はゼロ埋めする
			memset(mc.data, 0x00, mc.length);
		}
		process->write(mc);
	}

	process->finishWrite();
	int ret = process->join();
	if (ret != 0) {
		ctx.error("↓↓↓↓↓↓音声エンコーダ最後の出力↓↓↓↓↓↓");
		for (auto v : process->getLastLines()) {
			v.push_back(0); // null terminate
			ctx.errorF("%s", v.data());
		}
		ctx.error("↑↑↑↑↑↑音声エンコーダ最後の出力↑↑↑↑↑↑");
		THROWF(RuntimeException, "音声エンコーダ終了コード: 0x%x", ret);
	}
}
