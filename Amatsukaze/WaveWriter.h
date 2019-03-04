/**
* Wave file writer
* Copyright (c) 2017-2019 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include <stdint.h>
#include <stdio.h>

#include "StreamUtils.hpp"

struct RiffHeader {
	uint32_t riff;
	int32_t  size;
	uint32_t type;
};

struct FormatChunk {
	uint32_t id;
	int32_t size;
	int16_t format;
	uint16_t channels;
	uint32_t samplerate;
	uint32_t bytepersec;
	uint16_t blockalign;
	uint16_t bitswidth;
};

struct DataChunk {
	uint32_t id;
	int32_t size;
	// uint8_t waveformData[];
};

enum {
	WAVE_HEADER_LENGTH = sizeof(RiffHeader) + sizeof(FormatChunk) + sizeof(DataChunk)
};

uint32_t toBigEndian(uint32_t a) {
	return (a >> 24) | ((a & 0xFF0000) >> 8) | ((a & 0xFF00) << 8) | (a << 24);
}

// numSamples: 1チャンネルあたりのサンプル数
// エラー検出のため numSamples が64bitになっているがintを超える範囲に対応している訳ではないことに注意
void writeWaveHeader(FILE* fp, int channels, int samplerate, int bitswidth, int64_t numSamples)
{
	// サイズチェック
	// 全体のサイズが2^31 - 1バイトを超えてはならない
	const int64_t MAX_SIZE = (int64_t(1) << 32) - 1;

	int headerSize = sizeof(RiffHeader) + sizeof(FormatChunk) + 8;
	int64_t dataSize = channels * (bitswidth / 8) * numSamples;
	int64_t fileSize = headerSize + dataSize;

	if (fileSize > MAX_SIZE) {
		THROW(FormatException, "2GB超のWaveデータファイルはサポートしていません");
	}

	struct Header {
		RiffHeader riffHeader;
		FormatChunk formatChunk;
		DataChunk dataChunk;
	} header = {
		{ // RiffHeader
			toBigEndian('RIFF'),
			(int)fileSize - 8,
			toBigEndian('WAVE')
		},
		{ // FormatChunk
			toBigEndian('fmt '),
			sizeof(FormatChunk) - 8,
			0x0001, // Microsoft PCM format
			(uint16_t)channels,
			(uint32_t)samplerate,
			uint32_t(channels * samplerate * (bitswidth / 8)),
			uint16_t(channels * bitswidth),
			16
		},
		{ // DataChunk
			toBigEndian('data'),
			(int32_t)dataSize
		}
	};

	if (fwrite(&header, headerSize, 1, fp) != 1) {
		throw IOException("Waveヘッダの書き込みに失敗");
	}
}

