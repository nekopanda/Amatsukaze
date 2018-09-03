/**
* Encoder Option Parser
* Copyright (c) 2017-2018 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include <string>
#include <regex>

#include "StringUtils.hpp"
#include "TranscodeSetting.hpp"
#include "avisynth.h"

enum ENUM_ENCODER_DEINT {
	ENCODER_DEINT_NONE,
	ENCODER_DEINT_30P,
	ENCODER_DEINT_24P,
	ENCODER_DEINT_60P,
	ENCODER_DEINT_VFR
};

struct EncoderOptionInfo {
	VIDEO_STREAM_FORMAT format;
	ENUM_ENCODER_DEINT deint;
	bool afsTimecode;
};

static std::vector<std::wstring> SplitOptions(const tstring& str)
{
	std::wstring wstr = to_wstring(str);
	std::wregex re(L"(([^\" ]+)|\"([^\"]+)\") *");
	std::wsregex_iterator it(wstr.begin(), wstr.end(), re);
	std::wsregex_iterator end;
	std::vector<std::wstring> argv;
	for (; it != end; ++it) {
		if ((*it)[2].matched) {
			argv.push_back((*it)[2].str());
		}
		else if ((*it)[3].matched) {
			argv.push_back((*it)[3].str());
		}
	}
	return argv;
}

EncoderOptionInfo ParseEncoderOption(ENUM_ENCODER encoder, const tstring& str)
{
	EncoderOptionInfo info = EncoderOptionInfo();

	if (encoder == ENCODER_X264) {
		info.format = VS_H264;
		return info;
	}
	else if (encoder == ENCODER_X265) {
		info.format = VS_H265;
		return info;
	}

	auto argv = SplitOptions(str);
	int argc = (int)argv.size();

	info.format = VS_H264;

	for (int i = 0; i < argc; ++i) {
		auto& arg = argv[i];
		auto& next = (i + 1 < argc) ? argv[i + 1] : L"";
		if (arg == L"--vpp-deinterlace") {
			if (next == L"normal" || next == L"adaptive") {
				info.deint = ENCODER_DEINT_30P;
			}
			else if (next == L"it") {
				info.deint = ENCODER_DEINT_24P;
			}
			else if (next == L"bob") {
				info.deint = ENCODER_DEINT_60P;
			}
		}
		else if (arg == L"--vpp-afs") {
			bool is24 = false;
			bool timecode = false;
			bool drop = false;
			std::wregex re(L"([^=]+)=([^,]+),?");
			std::wsregex_iterator it(next.begin(), next.end(), re);
			std::wsregex_iterator end;
			std::vector<std::wstring> argv;
			for (; it != end; ++it) {
				auto key = (*it)[1].str();
				auto val = (*it)[2].str();
				std::transform(val.begin(), val.end(), val.begin(), ::tolower);
				if (key == L"24fps") {
					is24 = (val == L"1" || val == L"true");
				}
				else if (key == L"drop") {
					drop = (val == L"1" || val == L"true");
				}
				else if (key == L"timecode") {
					timecode = (val == L"1" || val == L"true");
				}
				else if (key == L"preset") {
					is24 = (val == L"24fps");
					drop = (val == L"double" || val == L"anime" ||
						val == L"cinema" || val == L"min_afterimg" || val == L"24fps");
				}
			}
			if (is24 && !drop) {
				THROW(ArgumentException, 
					"vpp-afsオプションに誤りがあります。24fps化する場合は間引き(drop)もonにする必要があります");
			}
			if (drop && !timecode) {
				THROW(ArgumentException,
					"vpp-afsで間引き(drop)がonの場合はタイムコード(timecode=true)が必須です");
			}
			if (timecode) {
				info.deint = ENCODER_DEINT_VFR;
				info.afsTimecode = true;
			}
			else {
				info.deint = is24 ? ENCODER_DEINT_24P : ENCODER_DEINT_30P;
				info.afsTimecode = false;
			}
		}
		else if (arg == L"-c" || arg == L"--codec") {
			if (next == L"h264") {
				info.format = VS_H264;
			}
			else if (next == L"hevc") {
				info.format = VS_H265;
			}
			else if (next == L"mpeg2") {
				info.format = VS_MPEG2;
			}
			else {
				info.format = VS_UNKNOWN;
			}
		}
	}

	return info;
}

void PrintEncoderInfo(AMTContext& ctx, EncoderOptionInfo info) {
	switch (info.deint) {
	case ENCODER_DEINT_NONE:
		ctx.info("エンコーダでのインタレ解除: なし");
		break;
	case ENCODER_DEINT_24P:
		ctx.info("エンコーダでのインタレ解除: 24fps化");
		break;
	case ENCODER_DEINT_30P:
		ctx.info("エンコーダでのインタレ解除: 30fps化");
		break;
	case ENCODER_DEINT_60P:
		ctx.info("エンコーダでのインタレ解除: 60fps化");
		break;
	case ENCODER_DEINT_VFR:
		ctx.info("エンコーダでのインタレ解除: VFR化");
		break;
	}
}
