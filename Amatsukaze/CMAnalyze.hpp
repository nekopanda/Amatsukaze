#pragma once

#include <fstream>
#include <string>
#include <iostream>
#include <memory>

#include "StreamUtils.hpp"
#include "TranscodeSetting.hpp"

class CMAnalyze : public AMTObject
{
public:
	CMAnalyze(AMTContext& ctx,
		const TranscoderSetting& setting)
		: AMTObject(ctx)
		, setting_(setting)
	{
		//
	}

	void analyze(int videoFileIndex)
	{
		std::ofstream out(setting_.getTmpSourceAVSPath(videoFileIndex));
		std::string dllpath;
		std::string amtspath = setting_.getTmpAMTSourcePath(videoFileIndex);
		out << "AMTSource(\"" << amtspath  << "\")" << std::endl;

		// TODO:
	}

private:
	const TranscoderSetting& setting_;
};
