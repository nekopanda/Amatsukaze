/**
* MPEG2 Video parser
* Copyright (c) 2017 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include "StreamUtils.hpp"

#define SEQ_HEADER_START_CODE 0x000001B3
#define PICTURE_START_CODE 0x00000100
#define EXTENSION_START_CODE 0x000001B5

static bool mpeg2_next_start_code(BitReader& reader) {
	reader.byteAlign();
	while (reader.next<24>() != 1) {
		if (reader.read<8>() != 0) {
			return false;
		}
	}
	return true;
}

struct MPEG2SequenceHeader {

	bool parse(uint8_t *data, int length) {
		BitReader reader(MemoryChunk(data, length));
		try {
			// Sequence header
			uint32_t sequence_header_code = reader.read<32>();
			if (sequence_header_code != SEQ_HEADER_START_CODE) {
				return false;
			}
			horizontal_size_value = reader.read<12>();
			vertical_size_value = reader.read<12>();
			aspect_ratio_info = reader.read<4>();
			frame_rate_code = reader.read<4>();
			bit_rate_value = reader.read<18>();
			if (!reader.read<1>()) return false; // marker bit
			vbv_buffer_size_value = reader.read<10>();
			constrained_parameters_flag = reader.read<1>();
			if (reader.read<1>()) { // load_intra_quantiser_matrix
				reader.skip(8 * 64);
			}
			if (reader.read<1>()) { // load_non_intra_quantiser_matrix
				reader.skip(8 * 64);
			}
			if (!mpeg2_next_start_code(reader)) return false;

			// Sequence extension
			uint32_t extension_start_code = reader.read<32>();
			if (extension_start_code != EXTENSION_START_CODE) {
				return false;
			}
			uint8_t extensin_start_code_identifier = reader.read<4>();
			if (extensin_start_code_identifier != 0x1) { // Sequence Extention ID
				return false;
			}
			profile_and_level_indication = reader.read<8>();
			progressive_sequence = reader.read<1>();
			chroma_format = reader.read<2>();
			horizontal_size_extension = reader.read<2>();
			vertical_size_extension = reader.read<2>();
			bit_rate_extension = reader.read<12>();
			if (!reader.read<1>()) return false; // marker bit
			vbv_buffer_size_extension = reader.read<8>();
			low_delay = reader.read<1>();
			frame_rate_extension_n = reader.read<2>();
			frame_rate_extension_d = reader.read<5>();
			if (!mpeg2_next_start_code(reader)) return false;

			numReadBytes = reader.numReadBytes();

			// Sequence Display Extension
			has_sequence_display_extension = false;
			extension_start_code = reader.read<32>();
			if (extension_start_code != EXTENSION_START_CODE) {
				return true;
			}
			extensin_start_code_identifier = reader.read<4>();
			if (extensin_start_code_identifier != 0x2) { // Sequence Display Extention ID
				return true;
			}
			has_sequence_display_extension = true;
			video_format = reader.read<3>();
			colour_description = reader.read<1>();
			if (colour_description) {
				colour_primaries = reader.read<8>();
				transfer_characteristics = reader.read<8>();
				matrix_coefficients = reader.read<8>();
			}
			display_horizontal_size = reader.read<14>();
			reader.read<1>(); // marker_bit
			display_vertical_size = reader.read<14>();
			if (!mpeg2_next_start_code(reader)) return false;

			numReadBytes = reader.numReadBytes();
		}
		catch (const EOFException&) {
			return false;
		}
		catch (const FormatException&) {
			return false;
		}
		return true;
	}
	bool check() { return true; }

	// Sequence header
	uint16_t horizontal_size_value;
	uint16_t vertical_size_value;
	uint8_t aspect_ratio_info;
	uint8_t frame_rate_code;
	uint32_t bit_rate_value;
	uint16_t vbv_buffer_size_value;
	uint16_t constrained_parameters_flag;

	// Sequence extension
	uint8_t profile_and_level_indication;
	uint8_t progressive_sequence;
	uint8_t chroma_format;
	uint8_t horizontal_size_extension;
	uint8_t vertical_size_extension;
	uint16_t bit_rate_extension;
	uint8_t vbv_buffer_size_extension;
	uint8_t low_delay;
	uint8_t frame_rate_extension_n;
	uint8_t frame_rate_extension_d;

	// Sequence Display Extension
	uint8_t video_format;
	uint8_t colour_description;
	uint8_t colour_primaries;
	uint8_t transfer_characteristics;
	uint8_t matrix_coefficients;
	uint16_t display_horizontal_size;
	uint16_t display_vertical_size;

	bool has_sequence_display_extension;

	int numReadBytes;

	int width() {
		return (horizontal_size_extension << 12) | horizontal_size_value;
	}
	int height() {
		return (vertical_size_extension << 12) | vertical_size_value;
	}
	std::pair<int, int> frame_rate() {
		auto base = frame_rate_value(frame_rate_code);
		return std::make_pair(
			base.first * (frame_rate_extension_n + 1),
			base.second * (frame_rate_extension_d + 1));
	}
	void getSAR(int& sar_w, int& sar_h) {
		if (aspect_ratio_info == 1) {
			// Square Sample
			sar_w = sar_h = 1;
			return;
		}
		
		// DAR取得
		int dar_w, dar_h;
		getDAR(dar_w, dar_h);

		// DAR対象の領域を取得
		int dw, dh;
		if (has_sequence_display_extension) {
			dw = display_horizontal_size;
			dh = display_vertical_size;
		}
		else {
			dw = width();
			dh = height();
		}

		// アスペクト比を計算
		sar_w = dar_w * dh;
		sar_h = dar_h * dw;
		int denom = gcd(sar_w, sar_h);
		sar_w /= denom;
		sar_h /= denom;
	}

private:
	void getDAR(int& width, int& height) {
		switch (aspect_ratio_info) {
		case 2:
			width = 4; height = 3;
			return;
		case 3:
			width = 16; height = 9;
			return;
		case 4:
			width = 42; height = 19;
			return;
		}
		width = 16; height = 9;
	}

	static std::pair<int, int> frame_rate_value(int code) {
		switch (code) {
		case 1: return std::make_pair(24000, 1001);
		case 2: return std::make_pair(24, 1);
		case 3: return std::make_pair(25, 1);
		case 4: return std::make_pair(30000, 1001);
		case 5: return std::make_pair(30, 1);
		case 6: return std::make_pair(50, 1);
		case 7: return std::make_pair(60000, 1001);
		case 8: return std::make_pair(60, 1);
		}
		THROW(FormatException, "Unknown frame rate value");
		return std::make_pair(0, 1);
	}

	int gcd(int a, int b) {
		if (a > b) return gcd_(a, b);
		else if (a < b) return gcd_(b, a);
		return a;
	}

	// a > b
	int gcd_(int a, int b) {
		if (b == 0) return a;
		return gcd_(b, a % b);
	}
};

struct MPEG2PictureHeader {

	bool parse(uint8_t *data, int length) {
		BitReader reader(MemoryChunk(data, length));
		try {
			// Picture header
			uint32_t sequence_header_code = reader.read<32>();
			if (sequence_header_code != PICTURE_START_CODE) {
				return false;
			}
			temporal_reference = reader.read<10>();
			picture_coding_type = reader.read<3>();
			vbv_delay = reader.read<16>();

			if (picture_coding_type == 2 || picture_coding_type == 3) {
				reader.skip(4);
			}
			if (picture_coding_type == 3) {
				reader.skip(4);
			}
			while (reader.read<1>()) reader.skip(8);
			if (!mpeg2_next_start_code(reader)) return false;

			// Picture Coding Extension
			uint32_t _extension_start_code = reader.read<32>();
			if (_extension_start_code != EXTENSION_START_CODE) {
				return false;
			}
			uint8_t extensin_start_code_identifier = reader.read<4>();
			if (extensin_start_code_identifier != 0x8) { // Picture Coding Extension ID
				return false;
			}
			reader.skip(16); // f_code
			intra_dc_precesion = reader.read<2>();
			picture_structure = reader.read<2>();
			top_field_first = reader.read<1>();
			frame_pred_frame_dct = reader.read<1>();
			concealment_motion_vectors = reader.read<1>();
			q_scale_type = reader.read<1>();
			intra_vlc_format = reader.read<1>();
			alternate_scan = reader.read<1>();
			repeat_first_field = reader.read<1>();
			chroma_420_type = reader.read<1>();
			progressive_frame = reader.read<1>();
			composite_display_flag = reader.read<1>();

			numReadBytes = reader.numReadBytes();
		}
		catch (const EOFException&) {
			return false;
		}
		catch (const FormatException&) {
			return false;
		}
		return true;
	}
	bool check() { return true; }

	// Picture header
	uint16_t temporal_reference;
	uint8_t picture_coding_type;
	uint16_t vbv_delay;

	// Picture conding extension
	uint8_t intra_dc_precesion;
	uint8_t picture_structure;
	uint8_t top_field_first;
	uint8_t frame_pred_frame_dct;
	uint8_t concealment_motion_vectors;
	uint8_t q_scale_type;
	uint8_t intra_vlc_format;
	uint8_t alternate_scan;
	uint8_t repeat_first_field;
	uint8_t chroma_420_type;
	uint8_t progressive_frame;
	uint8_t composite_display_flag;

	int numReadBytes;
};

class MPEG2VideoParser : public IVideoParser {
public:
	virtual void reset() {
		hasSequenceHeader = false;
	}

	virtual bool inputFrame(MemoryChunk frame, std::vector<VideoFrameInfo>& info, int64_t PTS, int64_t DTS) {
		info.clear();

		int receivedField = 0;
		bool isGopStart = false;
		bool progressive = false;
		PICTURE_TYPE picType = PIC_FRAME;
		FRAME_TYPE type = FRAME_NO_INFO;
    int codedDataSize = (int)frame.length;

		for (int b = 0; b <= (int)frame.length - 4; ++b) {
			switch (read32(&frame.data[b])) {
			case SEQ_HEADER_START_CODE:
				if (sequenceHeader.parse(&frame.data[b], (int)frame.length - b)) {
					format.width = sequenceHeader.width();
					format.height = sequenceHeader.height();
					sequenceHeader.getSAR(format.sarWidth, format.sarHeight);
          auto frameRate = sequenceHeader.frame_rate();
          format.format = VS_MPEG2;
					format.frameRateNum = frameRate.first;
					format.frameRateDenom = frameRate.second;
					format.fixedFrameRate = true;
					format.progressive = (sequenceHeader.progressive_sequence != 0);

					if (sequenceHeader.colour_description == 0) {
						format.colorPrimaries = 2;
						format.transferCharacteristics = 2;
						format.colorSpace = 2;
					}
					else {
						format.colorPrimaries = sequenceHeader.colour_primaries;
						format.transferCharacteristics = sequenceHeader.transfer_characteristics;
						format.colorSpace = sequenceHeader.matrix_coefficients;
					}
					
					b += sequenceHeader.numReadBytes;
					hasSequenceHeader = true;
					isGopStart = true;
				}
				break;
			case PICTURE_START_CODE:
				MPEG2PictureHeader& picHeader = pictureHeader[receivedField++];
				if (picHeader.parse(&frame.data[b], (int)frame.length - b)) {
					if (receivedField == 1) { // 1枚目
						switch (picHeader.picture_structure) {
						case 1: // Top Field
							picType = PIC_TFF;
							break;
						case 2: // Bottom Field
							picType = PIC_BFF;
							break;
						case 3: // Frame picture
							if (sequenceHeader.progressive_sequence) {
								if (picHeader.repeat_first_field == 0) {
									picType = PIC_FRAME;
								}
								else if (picHeader.top_field_first == 0) {
									picType = PIC_FRAME_DOUBLING;
								}
								else {
									picType = PIC_FRAME_TRIPLING;
								}
							}
							else if (picHeader.repeat_first_field == 0) {
								picType = picHeader.top_field_first ?
									PIC_TFF : PIC_BFF;
							}
							else { // repeat_first_field == 1
								picType = picHeader.top_field_first ?
									PIC_TFF_RFF : PIC_BFF_RFF;
							}
							++receivedField;
							break;
						}
						switch (picHeader.picture_coding_type) {
						case 1:
							type = FRAME_I;
							break;
						case 2:
							type = FRAME_P;
							break;
						case 3:
							type = FRAME_B;
							break;
						}
						progressive = (picHeader.progressive_frame != 0);
					}
					else {
						// 2枚目はチェック可能だけど面倒なので見ない
						if (picHeader.picture_structure == 3) {
							THROW(FormatException, "フィールド配置が変則的すぎて対応できません");
						}
						switch (picType) {
						case PIC_TFF:
							if (picHeader.picture_structure != 2) {
								THROW(FormatException, "フィールド配置が変則的すぎて対応できません");
							}
							break;
						case PIC_BFF:
							if (picHeader.picture_structure != 1) {
								THROW(FormatException, "フィールド配置が変則的すぎて対応できません");
							}
							break;
						}
					}
					b += picHeader.numReadBytes;
				}

				if (receivedField > 2) {
					THROW(FormatException, "フィールド配置が変則的すぎて対応できません");
				}

				if (receivedField == 2) {
					// 1フレーム受信した
					VideoFrameInfo finfo;
					finfo.format = format;
					finfo.isGopStart = isGopStart;
					finfo.progressive = progressive;
					finfo.pic = picType;
					finfo.type = type;
					finfo.DTS = DTS;
					finfo.PTS = PTS;
          finfo.codedDataSize = codedDataSize;
					info.push_back(finfo);

					// 初期化しておく
					receivedField = 0;
					isGopStart = false;
					picType = PIC_FRAME;
					type = FRAME_NO_INFO;
          codedDataSize = 0;
				}

				break;
			}
		}

		return info.size() > 0;
	}

private:
	bool hasSequenceHeader;
	MPEG2SequenceHeader sequenceHeader;
	MPEG2PictureHeader pictureHeader[2];
	VideoFormat format;
};
