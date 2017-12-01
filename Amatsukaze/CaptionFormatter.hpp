/**
* Caption Formatter
* Copyright (c) 2017 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include "StreamReform.hpp"

class CaptionASSFormatter : public AMTObject
{
public:
	CaptionASSFormatter(AMTContext& ctx)
		: AMTObject(ctx)
	{
		DefFontSize = 36;

		initialState.x = 0;
		initialState.y = 0;
		initialState.fsx = 1;
		initialState.fsy = 1;
		initialState.spacing = 4;
		initialState.textColor = { 0xFF, 0xFF, 0xFF, 0xFF };
		initialState.backColor = { 0, 0, 0, 0x80 };
		initialState.style = 0;
	}

	std::wstring generate(const std::vector<OutCaptionLine>& lines) {
		sb.clear();
		PlayResX = lines[0].line->planeW;
		PlayResY = lines[0].line->planeH;
		header();
		for (int i = 0; i < (int)lines.size(); ++i) {
			item(lines[i]);
		}
		return sb.str();
	}

private:
	struct FormatState {
		int x, y;
		float fsx, fsy;
		int spacing;
		CLUT_DAT_DLL textColor;
		CLUT_DAT_DLL backColor;
		int style;
	};

	StringBuilderW sb;
	StringBuilderW attr;
	int PlayResX;
	int PlayResY;
	float DefFontSize;

	FormatState initialState;
	FormatState curState;

	void header() {
		sb.append(L"[Script Info]\n")
			.append(L"ScriptType: v4.00+\n")
			.append(L"Collisions: Normal\n")
			.append(L"ScaledBorderAndShadow: Yes\n")
			.append(L"PlayResX: %d\n", (int)PlayResX)
			.append(L"PlayResY: %d\n", (int)PlayResY)
			.append(L"\n")
			.append(L"[V4+ Styles]\n")
			.append(L"Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, "
				"Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, "
				"BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n")
			.append(L"Style: Default,Yu Gothic,%d,&H00FFFFFF,&H000000FF,&H00000000,&H7F000000,"
				"1,0,0,0,100,100,4,"
				"0,1,2,2,1,0,0,0,1\n", (int)DefFontSize + 10) // なぜかYu Gothicは+10しないとそのサイズにならなかった
			.append(L"\n")
			.append(L"[Events]\n")
			.append(L"Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n");
	}

	void item(const OutCaptionLine& line) {
		if (line.line->formats.size() == 0) {
			return;
		}

		curState = initialState;

		sb.append(L"Dialogue: 0,");
		time(line.start);
		sb.append(L",");
		time(line.end);
		sb.append(L",Default,,0000,0000,0000,,");
		
		// 途中で解像度が変わったとき用
		float scalex = (float)PlayResX / line.line->planeW;
		float scaley = (float)PlayResY / line.line->planeH;

		auto& fmts = line.line->formats;
		int nfrags = (int)fmts.size();
		auto& text = line.line->text;
		
		for (int i = 0; i < nfrags; ++i) {
			int begin = fmts[i].pos;
			int end = (i + 1 < nfrags) ? fmts[i + 1].pos : (int)text.size();
			auto& fmt = fmts[i];
			auto& fragtext = std::wstring(text.begin() + begin, text.begin() + end);

			if (i == 0) {
				int len = StrlenWoLoSurrogate(fragtext.c_str());
				float x = line.line->posX + (fmt.width / len - fmt.charW) * DefFontSize / fmt.charW / 2;
				float y = line.line->posY - (fmt.height - fmt.charH) / 2;
				// posは先頭でしか効果がない模様
				setPos((int)(x * scalex), (int)(y * scaley));
			}

			fragment(scalex, scaley, fragtext, line.line->formats[i]);
		}

		sb.append(L"\n");
	}

	void fragment(float scalex, float scaley, const std::wstring& text, const CaptionFormat& fmt) {
		int len = StrlenWoLoSurrogate(text.c_str());
		float fsx = fmt.charW / DefFontSize;
		float fsy = fmt.charH / DefFontSize;
		float spacing = (fmt.width / len - fmt.charW) / fsx;

		setColor(fmt.textColor, fmt.backColor);
		setFontSize(fsx * scalex, fsy * scaley);
		setSpacing((int)std::round(spacing * scalex));
		setStyle(fmt.style);
		
		if (attr.getMC().length > 0) {
			// オーバーライドコード出力
			sb.append(L"{%s}", attr.str());
			attr.clear();
		}
		sb.append(L"%s", text);
	}

	void time(double t) {
		double totalSec = t / MPEG_CLOCK_HZ;
		double totalMin = totalSec / 60;
		int h = (int)(totalMin / 60);
		int m = (int)totalMin % 60;
		double sec = totalSec - (int)totalMin * 60;
		sb.append(L"%d:%02d:%02.2f", h, m, sec);
	}

	void setPos(int x, int y) {
		if (curState.x != x || curState.y != y) {
			attr.append(L"\\pos(%d,%d)", x, y);
			curState.x = x;
			curState.y = y;
		}
	}

	void setColor(CLUT_DAT_DLL textColor, CLUT_DAT_DLL backColor) {
		if (!(curState.textColor == textColor)) {
			attr.append(L"\\c&H%02X%02X%02X%02X",
				255 - textColor.ucAlpha, textColor.ucB, textColor.ucG, textColor.ucR);
			curState.textColor = textColor;
		}
		if (!(curState.backColor == backColor)) {
			attr.append(L"\\4c&H%02X%02X%02X%02X",
				255 - backColor.ucAlpha, backColor.ucB, backColor.ucG, backColor.ucR);
			curState.backColor = backColor;
		}
	}

	void setFontSize(float fsx, float fsy) {
		if (curState.fsx != fsx) {
			attr.append(L"\\fscx%d",(int)(fsx * 100));
			curState.fsx = fsx;
		}
		if (curState.fsy != fsy) {
			attr.append(L"\\fscy%d", (int)(fsy * 100));
			curState.fsy = fsy;
		}
	}

	void setSpacing(int spacing) {
		if (curState.spacing != spacing) {
			attr.append(L"\\fsp%d", spacing);
			curState.spacing = spacing;
		}
	}

	void setStyle(int style) {
		bool cUnderline = (curState.style & CaptionFormat::UNDERLINE) != 0;
		bool nUnderline = (style & CaptionFormat::UNDERLINE) != 0;
		//bool cShadow = (curState.style & CaptionFormat::SHADOW) != 0;
		//bool nShadow = (style & CaptionFormat::SHADOW) != 0;
		bool cBold = (curState.style & CaptionFormat::BOLD) != 0;
		bool nBold = (style & CaptionFormat::BOLD) != 0;
		bool cItalic = (curState.style & CaptionFormat::ITALIC) != 0;
		bool nItalic = (style & CaptionFormat::ITALIC) != 0;
		if (cUnderline != nUnderline) {
			attr.append(L"\\u%d", (int)nUnderline);
		}
		//if (cShadow != nShadow) {
		//	attr.append(L"\\u%d", (int)nShadow);
		//}
		if (cBold != nBold) {
			attr.append(L"\\b%d", (int)nBold);
		}
		if (cItalic != nItalic) {
			attr.append(L"\\i%d", (int)nItalic);
		}
		curState.style = style;
	}
};

class CaptionSRTFormatter : public AMTObject {
public:
	CaptionSRTFormatter(AMTContext& ctx)
		: AMTObject(ctx)
	{ }

	std::wstring generate(const std::vector<OutCaptionLine>& lines) {
		sb.clear();
		subIndex = 1;
		prevEnd = -1;
		prevPosY = -1;

		for (int i = 0; i < (int)lines.size(); ++i) {
			item(lines[i]);
		}
		pushLine();
		return sb.str();
	}

private:
	StringBuilderW sb;
	StringBuilderW linebuf;
	int subIndex;
	double prevEnd;
	float prevPosY;

	void pushLine() {
		auto str = linebuf.str();
		if (str.size() > 0) {
			sb.append(L"%s\n", str);
			linebuf.clear();
		}
	}

	void time(double t) {
		double totalSec = t / MPEG_CLOCK_HZ;
		double totalMin = totalSec / 60;
		int h = (int)(totalMin / 60);
		int m = (int)totalMin % 60;
		double sec = totalSec - (int)totalMin * 60;
		int s = (int)sec;
		int ss = (int)std::round(((sec - s) * 1000));
		sb.append(L"%02d:%02d:%02d,%03d", h, m, s, ss);
	}

	void item(const OutCaptionLine& line) {
		if (line.line->formats.size() == 0) {
			return;
		}

		auto& fmts = line.line->formats;
		int nfrags = (int)fmts.size();
		auto& text = line.line->text;

		for (int i = 0; i < nfrags; ++i) {
			if (fmts[i].sizeMode == CP_STR_SMALL) {
				// 小サイズは出力しない
				continue;
			}
			if (line.end != prevEnd) {
				pushLine();
				// 時間を出力
				sb.append(L"\n%d\n", subIndex++);
				time(line.start);
				sb.append(L" --> ");
				time(line.end);
				sb.append(L"\n");
				prevEnd = line.end;
				prevPosY = -1;
			}
			if (line.line->posY != prevPosY) {
				// 改行
				pushLine();
				prevPosY = line.line->posY;
			}
			int begin = fmts[i].pos;
			int end = (i + 1 < nfrags) ? fmts[i + 1].pos : (int)text.size();
			auto& fragtext = std::wstring(text.begin() + begin, text.begin() + end);
			linebuf.append(L"%s", fragtext);
		}
	}

};
