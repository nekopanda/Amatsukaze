/**
* ADTS AAC parser
* Copyright (c) 2017-2018 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
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
			if (ID != 1) return false; // 固定
			uint8_t layer = reader.read<2>();
			if (layer != 0) return false; // 固定

			protection_absent = reader.read<1>();
			profile = reader.read<2>();
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

			if (frame_length < numBytesRead) return false; // ヘッダより短いのはおかしい
		}
		catch (const EOFException&) {
			return false;
		}
		catch (const FormatException&) {
			return false;
		}
		return true;
	}

	bool check() {

		return true;
	}

	uint8_t protection_absent;
	uint8_t profile;
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
		, hAacDec(NULL)
		, bytesConsumed_(0)
		, lastPTS_(-1)
		, syncOK(false)
	{
		createChannelsMap();
	}
	~AdtsParser() {
		closeDecoder();
	}

	virtual void reset() {
		decodedBuffer.release();
	}

	virtual bool inputFrame(MemoryChunk frame__, std::vector<AudioFrameData>& info, int64_t PTS) {
		info.clear();
		decodedBuffer.clear();

		// codedBufferは次にinputFrameが呼ばれるまでデータを保持する必要があるので
		// inputFrameの先頭で前のinputFrame呼び出しで読んだデータを消す
		codedBuffer.trimHead(bytesConsumed_);

		if (codedBuffer.size() >= (1 << 13)) {
			// 不正なデータが続くと処理されないデータが永遠と増えていくので
			// 増え過ぎたら捨てる
			// ヘッダのframe_lengthフィールドは13bitなのでそれ以上データがあったら
			// 完全に不正データ
			codedBuffer.clear();
		}

		int prevDataSize = (int)codedBuffer.size();
		codedBuffer.add(frame__);
		MemoryChunk frame = codedBuffer.get();

		if (frame.length < 7) {
			// データ不正
			return false;
		}

		if (lastPTS_ == -1 && PTS >= 0) {
			// 最初のPTS
			lastPTS_ = PTS;
			PTS = -1;
		}

		int ibytes = 0;
		bytesConsumed_ = 0;
		for (; ibytes < (int)frame.length - 1; ++ibytes) {
			uint16_t syncword = (read16(&frame.data[ibytes]) >> 4);
			if (syncword != 0xFFF) {
				syncOK = false;
			}
			else {
				uint8_t* ptr = frame.data + ibytes;
				int len = (int)frame.length - ibytes;

				// ヘッダーOKかつフレーム長だけのデータがある
				if (header.parse(ptr, len)
					&& header.frame_length <= len)
				{
					// ストリームを解析するのは面倒なのでデコードしちゃう
					if (hAacDec == NULL) {
						resetDecoder(MemoryChunk(ptr, len));
					}
					NeAACDecFrameInfo frameInfo;
					void* samples = NeAACDecDecode(hAacDec, &frameInfo, ptr, len);
					if (frameInfo.error != 0) {
						// フォーマットが変わるとエラーを吐くので初期化してもう１回食わせる
						// 変な使い方だけどNeroAAC君はストリームの途中で
						// フォーマットが変わることを想定していないんだから仕方ない
						//（fixed headerが変わらなくてもチャンネル構成が変わることがあるから読んでみないと分からない）
						resetDecoder(MemoryChunk(ptr, len));
						samples = NeAACDecDecode(hAacDec, &frameInfo, ptr, len);
					}
					if (frameInfo.error == 0) {
						// ダウンミックスしているので2chになるはず
						int numChannels = frameInfo.num_front_channels +
							frameInfo.num_back_channels + frameInfo.num_side_channels + frameInfo.num_lfe_channels;

						if (numChannels != 2) {
							// フォーマットが変わるとバグって2chにできないこともあるので、初期化してもう１回食わせる
							// 変な使い方だけどNeroAAC君はストリームの途中で(ry
							resetDecoder(MemoryChunk(ptr, len));
							samples = NeAACDecDecode(hAacDec, &frameInfo, ptr, len);

							numChannels = frameInfo.num_front_channels +
								frameInfo.num_back_channels + frameInfo.num_side_channels + frameInfo.num_lfe_channels;
						}

						if (frameInfo.error != 0 || numChannels != 2) {
							ctx.incrementCounter(AMT_ERR_DECODE_AUDIO);
							ctx.warn("音声フレームを正しくデコードできませんでした");
						}
						else {
							decodedBuffer.add(MemoryChunk((uint8_t*)samples, frameInfo.samples * 2));

							AudioFrameData frameData;
							frameData.numSamples = frameInfo.original_samples / numChannels;
							frameData.numDecodedSamples = frameInfo.samples / numChannels;
							frameData.format.channels = getAudioChannels(header, frameInfo);
							frameData.format.sampleRate = frameInfo.samplerate;
							
							// ストリームが正常なら frameInfo.bytesconsumed == header.frame_length となるはずだが
							// ストリームが不正だと同じにならないことがある
							// その場合、長さは header.frame_length を優先する
							//（その方が次のフレームが正しくデコードされる確率が上がるのと
							//  L-SMASHがheader.frame_lengthを見てフレームをスキップしているので
							//  これが実際のフレーム長と一致していないと落ちるので）
							//frameData.codedDataSize = frameInfo.bytesconsumed;
							frameData.codedDataSize = header.frame_length;

							// codedBuffer内データへのポインタを入れているので
							// codedBufferには触らないように注意！
							frameData.codedData = ptr;
							frameData.decodedDataSize = frameInfo.samples * 2;
							// AutoBufferはメモリ再確保があるのでデコードデータへのポインタは後で入れる

							// PTSを計算
							int64_t duration = 90000 * frameData.numSamples / frameData.format.sampleRate;
							if (ibytes < prevDataSize) {
								// フレームの開始が現在のパケット先頭より前だった場合
								// （つまり、PESパケットの境界とフレームの境界が一致しなかった場合）
								// 現在のパケットのPTSは適用できないので前のパケットからの値を入れる
								frameData.PTS = lastPTS_;
								lastPTS_ += duration;
								// 現在のパケットが来なければフレームを出力できなかったので、出力したフレームは現在のパケットの一部を含むはず
								ASSERT(ibytes + header.frame_length > prevDataSize);
								// つまり、PTSは（もしあれば）直後のフレームのPTSである
								if (PTS >= 0) {
									lastPTS_ = PTS;
									PTS = -1;
								}
							}
							else {
								// PESパケットの境界とフレームの境界が一致した場合
								// もしくはPESパケットの2番目以降のフレーム
								if (PTS >= 0) {
									lastPTS_ = PTS;
									PTS = -1;
								}
								frameData.PTS = lastPTS_;
								lastPTS_ += duration;
							}

							info.push_back(frameData);

							// データを進める
							ASSERT(frameInfo.bytesconsumed == header.frame_length);
							ibytes += header.frame_length - 1;
							bytesConsumed_ = ibytes + 1;

							syncOK = true;
						}
					}
				}
				else {
					// ヘッダ不正 or 十分なデータがなかった
					if (syncOK) {
						// 直前のフレームがOKなら単に次のパケットを受信すればいいだけ
						break;
					}
				}

			}
		}

		// デコードデータのポインタを入れる
		uint8_t* decodedData = decodedBuffer.ptr();
		for (int i = 0; i < (int)info.size(); ++i) {
			info[i].decodedData = (uint16_t*)decodedData;
			decodedData += info[i].decodedDataSize;
		}
		ASSERT(decodedData - decodedBuffer.ptr() == decodedBuffer.size());

		return info.size() > 0;
	}

private:
	NeAACDecHandle hAacDec;
	AdtsHeader header;
	std::map<int64_t, AUDIO_CHANNELS> channelsMap;

	// パケット間での情報保持
	AutoBuffer codedBuffer;
	int bytesConsumed_;
	int64_t lastPTS_;

	AutoBuffer decodedBuffer;
	bool syncOK;

	void closeDecoder() {
		if (hAacDec != NULL) {
			NeAACDecClose(hAacDec);
			hAacDec = NULL;
		}
	}

	bool resetDecoder(MemoryChunk data) {
		closeDecoder();

		hAacDec = NeAACDecOpen();
		NeAACDecConfigurationPtr conf = NeAACDecGetCurrentConfiguration(hAacDec);
		conf->outputFormat = FAAD_FMT_16BIT;
		conf->downMatrix = 1; // WAV出力は解析用なので2chあれば十分
		NeAACDecSetConfiguration(hAacDec, conf);

		unsigned long samplerate;
		unsigned char channels;
		if (NeAACDecInit(hAacDec, data.data, (int)data.length, &samplerate, &channels)) {
			ctx.warn("NeAACDecInitに失敗");
			return false;
		}
		return true;
	}

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

// デュアルモノAACを2つのAACに無劣化分離する
class DualMonoSplitter : AMTObject
{
public:
	DualMonoSplitter(AMTContext& ctx)
		: AMTObject(ctx)
		, hAacDec(NULL)
	{ }

	~DualMonoSplitter() {
		closeDecoder();
	}

	void inputPacket(MemoryChunk frame)
	{
		AdtsHeader header;
		if (!header.parse(frame.data, (int)frame.length)) {
			THROW(FormatException, "[DualMonoSplitter] ヘッダをparseできなかった");
		}
		// ストリームを解析するのは面倒なのでデコードしちゃう
		if (hAacDec == NULL) {
			resetDecoder(MemoryChunk(frame.data, frame.length));
		}
		NeAACDecFrameInfo frameInfo;
		void* samples = NeAACDecDecode(hAacDec, &frameInfo, frame.data, (int)frame.length);
		if (frameInfo.error != 0) {
			// ここでは大丈夫だとは思うけど一応エラー対策はやっておく
			resetDecoder(MemoryChunk(frame.data, frame.length));
			samples = NeAACDecDecode(hAacDec, &frameInfo, frame.data, (int)frame.length);
		}
		if (frameInfo.error == 0) {
			if (frameInfo.fr_ch_ele != 2) {
				THROWF(FormatException, "デュアルモノAACのエレメント数不正 %d != 2", frameInfo.fr_ch_ele);
			}

			for (int i = 0; i < 2; ++i) {
				BitWriter writer(buf);

				int start_bits = frameInfo.element_start[i];
				int end_bits = frameInfo.element_end[i];
				int frame_length = (end_bits - start_bits + 3 + 7) / 8 + 7;

				// ヘッダ
				writer.write<12>(0xFFF); // sync word
				writer.write<1>(1); // ID
				writer.write<2>(0); // layer
				writer.write<1>(1); // protection_absend
				writer.write<2>(header.profile); // profile
				writer.write<4>(header.sampling_frequency_index); // 
				writer.write<1>(0); // private bits
				writer.write<3>(1); // channel_configuration
				writer.write<1>(0); // original_copy
				writer.write<1>(0); // home
				writer.write<1>(0); // copyright_identification_bit
				writer.write<1>(0); // copyright_identification_start
				writer.write<13>(frame_length); // frame_length
				writer.write<11>((1 << 11) - 1); // adts_buffer_fullness(all ones means variable bit rate)
				writer.write<2>(0); // number_of_raw_data_blocks_in_frame

				// SPE１つ
				BitReader reader(frame);
				reader.skip(start_bits);
				int bitpos = start_bits;
				for (; bitpos + 32 <= end_bits; bitpos += 32) {
					writer.write<32>(reader.read<32>());
				}
				int remain = end_bits - bitpos;
				if (remain > 0) {
					writer.writen(reader.readn(remain), remain);
				}
				writer.write<3>(ID_END);
				writer.byteAlign<0>();
				writer.flush();

				if (buf.size() != frame_length) {
					THROW(RuntimeException, "サイズが合わない");
				}

				OnOutFrame(i, buf.get());
				buf.clear();
			}
		}
	}

	virtual void OnOutFrame(int index, MemoryChunk mc) = 0;

private:
	NeAACDecHandle hAacDec;
	AutoBuffer buf;

	void closeDecoder() {
		if (hAacDec != NULL) {
			NeAACDecClose(hAacDec);
			hAacDec = NULL;
		}
	}

	bool resetDecoder(MemoryChunk data) {
		closeDecoder();

		hAacDec = NeAACDecOpen();
		NeAACDecConfigurationPtr conf = NeAACDecGetCurrentConfiguration(hAacDec);
		conf->outputFormat = FAAD_FMT_16BIT;
		NeAACDecSetConfiguration(hAacDec, conf);

		unsigned long samplerate;
		unsigned char channels;
		if (NeAACDecInit(hAacDec, data.data, (int)data.length, &samplerate, &channels)) {
			ctx.warn("NeAACDecInitに失敗");
			return false;
		}
		return true;
	}
};
