#pragma once

#include <sstream>

#include "StreamReform.hpp"

class CaptionASSFormatter : public AMTObject
{
public:
	CaptionASSFormatter(AMTContext& ctx)
		: AMTObject(ctx)
	{
		PlayResX = 960;
		PlayResY = 540;
		DefFontSize = 36;

		initialState.x = 0;
		initialState.y = 0;
		initialState.textColor = { 0xFF, 0xFF, 0xFF, 0 };
		initialState.backColor = { 0, 0, 0, 0 };
		initialState.style = 0;
	}

	std::wstring generate(const std::vector<OutCaptionLine>& lines) {
		// TODO:
		ss = std::wstringstream();
		header();

	}

private:
	struct FormatState {
		int x, y;
		CLUT_DAT_DLL textColor;
		CLUT_DAT_DLL backColor;
		int style;
	};

	std::wstringstream ss;
	float PlayResX;
	float PlayResY;
	float DefFontSize;

	FormatState initialState;
	FormatState curState;

	void header() {
		ss << L"[Script Info]" << std::endl;
		ss << L"ScriptType: v4.00+" << std::endl;
		ss << L"Collisions: Normal" << std::endl;
		ss << L"ScaledBorderAndShadow: Yes" << std::endl;
		ss << L"PlayResX: " << (int)PlayResX << std::endl;
		ss << L"PlayResY: " << (int)PlayResY << std::endl;
		ss << std::endl;
		ss << L"[V4+ Styles]" << std::endl;
		ss << L"Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding" << std::endl;
		ss << L"Style: Default,MS UI Gothic," << (int)DefFontSize << L",&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,1,10,10,10,0" << std::endl;
		ss << std::endl;
		ss << L"[Events]" << std::endl;
		ss << L"Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text" << std::endl;
	}

	void item(const OutCaptionLine& line) {
		curState = initialState;

		ss << L"Dialogue: 0,";
		time(line.start);
		ss << ",";
		time(line.end);
		ss << L",Default,,0000,0000,0000,,";

		// TODO:
	}

	void time(double t) {
		double totalSec = t / MPEG_CLOCK_HZ;
		double totalMin = totalSec / 60;
		int h = (int)(totalMin / 60);
		int m = (int)totalMin % 60;
		double sec = totalSec - (int)totalMin * 60;
		ss << h << 
			L":" << std::setfill(L'0') << std::setw(2) << m << 
			L":" << std::setfill(L'0') << std::setw(5) << std::setprecision(2) << sec;
	}
};

