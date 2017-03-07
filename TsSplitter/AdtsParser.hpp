#pragma once

#include <stdint.h>

#include <vector>
#include <map>

#include "faad.h"

#include "StreamUtils.hpp"

enum AAC_SYNTAX_ELEMENTS {
	ID_SCE = 0x0,
	ID_CPE = 0x1,
	ID_CCE = 0x2,
	ID_LFE = 0x3,
	ID_DSE = 0x4,
	ID_PCE = 0x5,
	ID_FIL = 0x6,
	ID_END = 0x7,
};

#if 1
struct AdtsHeader {

	bool parse(uint8_t *data, int length) {
		// 長さチェック
		if (length < 7) return false;

		BitReader reader(MemoryChunk(data, length));
		try {
			uint16_t syncword = reader.read<12>();
			// sync word 不正
			if (syncword != 0xFFF) return false;

			uint8_t ID = reader.read<1>();
			uint8_t layer = reader.read<2>();
			// 固定
			if (layer != 0) return false;

			protection_absent = reader.read<1>();
			uint8_t profile = reader.read<2>();
			sampling_frequency_index = reader.read<4>();
			uint8_t private_bit = reader.read<1>();
			channel_configuration = reader.read<3>();
			uint8_t original_copy = reader.read<1>();
			uint8_t home = reader.read<1>();

			uint8_t copyright_identification_bit = reader.read<1>();
			uint8_t copyright_identification_start = reader.read<1>();
			frame_length = reader.read<13>();
			uint16_t adts_buffer_fullness = reader.read<11>();
			number_of_raw_data_blocks_in_frame = reader.read<2>();

			numBytesRead = reader.numReadBytes();
		}
		catch (EOFException) {
			return false;
		}
		catch (FormatException) {
			return false;
		}
		return true;
	}

	bool check() {

		return true;
	}

	uint8_t protection_absent;
	uint8_t sampling_frequency_index;
	uint8_t channel_configuration;
	uint16_t frame_length;
	uint8_t number_of_raw_data_blocks_in_frame;

	int numBytesRead;

	void getSamplingRate(int& rate) {
		switch (sampling_frequency_index) {
		case 0: rate = 96000; return;
		case 1: rate = 88200; return;
		case 2: rate = 64000; return;
		case 3: rate = 48000; return;
		case 4: rate = 44100; return;
		case 5: rate = 32000; return;
		case 6: rate = 24000; return;
		case 7: rate = 22050; return;
		case 8: rate = 16000; return;
		case 9: rate = 12000; return;
		case 0xa: rate = 11025; return;
		case 0xb: rate = 8000; return;
		default: return;
		}
	}
};
#endif

class AdtsParser : public AMTObject {
public:
	AdtsParser(AMTContext&ctx)
		: AMTObject(ctx)
		, hAacDec(NeAACDecOpen())
	{
		NeAACDecConfigurationPtr conf = NeAACDecGetCurrentConfiguration(hAacDec);
		conf->outputFormat = FAAD_FMT_16BIT;
		conf->downMatrix = 1; // WAV出力は解析用なので2chあれば十分
		NeAACDecSetConfiguration(hAacDec, conf);

		createChannelsMap();
	}
	~AdtsParser() {
		if (hAacDec != NULL) {
			NeAACDecClose(hAacDec);
			hAacDec = NULL;
		}
	}

	virtual void reset() {
		decodedBuffer.release();
	}

	virtual bool inputFrame(MemoryChunk frame, std::vector<AudioFrameData>& info, int64_t PTS) {
		info.clear();
		decodedBuffer.clear();

		if (frame.length < 7) {
			// データ不正
			return false;
		}

		int64_t curPTS = PTS;

		for (int i = 0; i < frame.length - 1; ++i) {
			uint16_t syncword = (read16(&frame.data[i]) >> 4);
			if (syncword == 0xFFF) {
				if (header.parse(frame.data + i, (int)frame.length - i)) {
					// ストリームを解析するのは面倒なのでデコードしちゃう
					NeAACDecFrameInfo frameInfo;
					void* samples = NeAACDecDecode(hAacDec, &frameInfo, frame.data + i, (int)frame.length - i);
					if (frameInfo.error != 0) {
						// フォーマットが変わるとエラーを吐くので初期化してもう１回食わせる
						// 変な使い方だけどNeroAAC君はストリームの途中で
						// フォーマットが変わることを想定していないんだから仕方ない
						//（fixed headerが変わらなくてもチャンネル構成が変わることがあるから読んでみないと分からない）
						// 面倒なので初回の初期化もこれで行う
						unsigned long samplerate;
						unsigned char channels;
						// TODO: フォーマットが変わる場合に対応できるか検証
						if (NeAACDecInit(hAacDec, frame.data + i, (int)frame.length - i, &samplerate, &channels)) {
							ctx.warn("NeAACDecInitに失敗");
							return false;
						}
						samples = NeAACDecDecode(hAacDec, &frameInfo, frame.data + i, (int)frame.length - i);
					}
					if (frameInfo.error == 0) {
						decodedBuffer.add((uint8_t*)samples, frameInfo.samples * 2);

						// ダウンミックスしているので2chになるはずだが
						int numChannels = frameInfo.num_front_channels +
							frameInfo.num_back_channels + frameInfo.num_side_channels + frameInfo.num_lfe_channels;

						AudioFrameData frameData;
						frameData.PTS = curPTS;
						frameData.numSamples = frameInfo.original_samples / numChannels;
						frameData.numDecodedSamples = frameInfo.samples / numChannels;
						frameData.format.channels = getAudioChannels(header, frameInfo);
						frameData.format.sampleRate = frameInfo.samplerate;
						frameData.codedDataSize = frameInfo.bytesconsumed;
						frameData.codedData = frame.data + i;
						frameData.decodedDataSize = frameInfo.samples * 2;
						// AutoBufferはメモリ再確保があるのでポインタは後で入れる
						info.push_back(frameData);

						curPTS += 90000 * frameData.numSamples / frameData.format.sampleRate;
						i += frameInfo.bytesconsumed - 1;
					}
				}

			}
		}

		// デコードデータのポインタを入れる
		uint8_t* decodedData = decodedBuffer.get();
		for (int i = 0; i < info.size(); ++i) {
			info[i].decodedData = (uint16_t*)decodedData;
			decodedData += info[i].decodedDataSize;
		}

		return info.size() > 0;
	}

private:
	NeAACDecHandle hAacDec;
	AdtsHeader header;
	std::map<int64_t, AUDIO_CHANNELS> channelsMap;

	AutoBuffer decodedBuffer;

	AUDIO_CHANNELS getAudioChannels(const AdtsHeader& header, const NeAACDecFrameInfo& frameInfo) {

		if (header.channel_configuration > 0) {
			switch (header.channel_configuration) {
			case 1: return AUDIO_MONO;
			case 2: return AUDIO_STEREO;
			case 3: return AUDIO_30;
			case 4: return AUDIO_31;
			case 5: return AUDIO_32;
			case 6: return AUDIO_32_LFE;
			case 7: return AUDIO_52_LFE; // 4K
			}
		}

		int64_t canonical = channelCanonical(frameInfo.fr_ch_ele, frameInfo.element_id);
		auto it = channelsMap.find(canonical);
		if (it == channelsMap.end()) {
			return AUDIO_NONE;
		}
		return it->second;
	}

	int64_t channelCanonical(int numElem, const uint8_t* elems) {
		int64_t canonical = -1;

		// canonicalにする上限（22.2chでも16個なので十分なはず）
		if (numElem > 20) {
			numElem = 20;
		}
		for (int i = 0; i < numElem; ++i) {
			canonical = (canonical << 3) | elems[i];
		}
		return canonical;
	}

	void createChannelsMap() {

		struct {
			AUDIO_CHANNELS channels;
			int numElem;
			const uint8_t elems[20];
		} table[] = {
			{
				AUDIO_21,
				2,{ (uint8_t)ID_CPE, (uint8_t)ID_SCE }
			},
			{
				AUDIO_22,
				2,{ (uint8_t)ID_CPE, (uint8_t)ID_CPE }
			},
			{
				AUDIO_2LANG,
				2,{ (uint8_t)ID_SCE, (uint8_t)ID_SCE }
			},
			// 以下4K
			{
				AUDIO_33_LFE,
				5,{ (uint8_t)ID_SCE, (uint8_t)ID_CPE, (uint8_t)ID_CPE, (uint8_t)ID_SCE, (uint8_t)ID_LFE }
			},
			{
				AUDIO_2_22_LFE,
				4,{ (uint8_t)ID_CPE, (uint8_t)ID_CPE, (uint8_t)ID_LFE, (uint8_t)ID_CPE }
			},
			{
				AUDIO_322_LFE,
				5,{ (uint8_t)ID_SCE, (uint8_t)ID_CPE, (uint8_t)ID_CPE, (uint8_t)ID_CPE, (uint8_t)ID_LFE }
			},
			{
				AUDIO_2_32_LFE,
				5,{ (uint8_t)ID_SCE, (uint8_t)ID_CPE, (uint8_t)ID_CPE, (uint8_t)ID_LFE, (uint8_t)ID_CPE }
			},
			{
				AUDIO_2_323_2LFE,
				8,{
					(uint8_t)ID_SCE, (uint8_t)ID_CPE, (uint8_t)ID_CPE, (uint8_t)ID_CPE,
					(uint8_t)ID_SCE, (uint8_t)ID_LFE, (uint8_t)ID_LFE, (uint8_t)ID_CPE
				}
			},
			{
				AUDIO_333_523_3_2LFE,
				16,{
					(uint8_t)ID_SCE, (uint8_t)ID_CPE, (uint8_t)ID_CPE, (uint8_t)ID_CPE, (uint8_t)ID_CPE, 
					(uint8_t)ID_SCE, (uint8_t)ID_LFE, (uint8_t)ID_LFE,
					(uint8_t)ID_SCE, (uint8_t)ID_CPE, (uint8_t)ID_CPE, (uint8_t)ID_SCE, (uint8_t)ID_CPE,
					(uint8_t)ID_SCE, (uint8_t)ID_SCE, (uint8_t)ID_CPE
				}
			}
		};

		channelsMap.clear();
		for (int i = 0; i < sizeof(table) / sizeof(table[0]); ++i) {
			int64_t canonical = channelCanonical(table[i].numElem, table[i].elems);
			ASSERT(channelsMap.find(canonical) == channelsMap.end());
			channelsMap[canonical] = table[i].channels;
		}
	}
};

