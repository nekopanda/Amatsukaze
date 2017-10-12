#pragma once

#include "TranscodeManager.hpp"

#include <cmath>

namespace logo {

static void approxim_line(int n, double sum_x, double sum_y, double sum_x2, double sum_xy, double& a, double& b)
{
  // doubleやfloatにはNaNが定義されているのでゼロ除算で例外は発生しない
	double temp = (double)n * sum_x2 - sum_x * sum_x;
	a = ((double)n * sum_xy - sum_x * sum_y) / temp;
	b = (sum_x2 * sum_y - sum_x * sum_xy) / temp;
}

class LogoColor
{
	double sumF, sumB, sumF2, sumB2, sumFB;
public:
	LogoColor()
		: sumF()
		, sumB()
		, sumF2()
		, sumB2()
		, sumFB()
	{ }

	// ピクセルの色を追加 f:前景 b:背景
	void Add(int f, int b)
	{
		sumF += f;
		sumB += b;
		sumF2 += f * f;
		sumB2 += b * b;
		sumFB += f * b;
	}

  // 値を0～1に正規化
  void Normalize(int maxv)
  {
    sumF /= (double)maxv;
    sumB /= (double)maxv;
    sumF2 /= (double)maxv*maxv;
    sumB2 /= (double)maxv*maxv;
    sumFB /= (double)maxv*maxv;
  }

	/*====================================================================
	* 	GetAB_?()
	* 		回帰直線の傾きと切片を返す X軸:前景 Y軸:背景
	*===================================================================*/
	bool GetAB(float& A, float& B, int data_count) const
  {
		double A1, A2;
		double B1, B2;
    approxim_line(data_count, sumF, sumB, sumF2, sumFB, A1, B1);
    approxim_line(data_count, sumB, sumF, sumB2, sumFB, A2, B2);

    // XY入れ替えたもの両方で平均を取る
		A = (float)((A1 + (1 / A2)) / 2);   // 傾きを平均
		B = (float)((B1 + (-B2 / A2)) / 2); // 切片も平均

    if (std::isnan(A) || std::isnan(B) || std::isinf(A) || std::isinf(B))
      return false;

		return true;
	}
};

class LogoData
{
  int w, h;
  int logUVx, logUVy;
  std::unique_ptr<float[]> data;
  float *aY, *aU, *aV;
  float *bY, *bU, *bV;
public:
  LogoData(int w, int h, int logUVx, int logUVy)
    : w(w), h(h), logUVx(logUVx), logUVy(logUVy)
  {
    int wUV = w >> logUVx;
    int hUV = h >> logUVy;
    data = std::unique_ptr<float[]>(new float[(w*h + wUV*hUV) * 2]);
    aY = data.get();
    bY = aY + w * h;
    aU = bY + w * h;
    bU = aU + wUV * hUV;
    aV = bU + wUV * hUV;
    bV = aV + wUV * hUV;
  }

  float* GetA(int plane) {
    switch (plane) {
    case PLANAR_Y: return aY;
    case PLANAR_U: return aU;
    case PLANAR_V: return aV;
    }
    return nullptr;
  }

  float* GetB(int plane) {
    switch (plane) {
    case PLANAR_Y: return bY;
    case PLANAR_U: return bU;
    case PLANAR_V: return bV;
    }
    return nullptr;
  }
};

class LogoScan
{
	int scanw;
	int scanh;
	int logUVx;
	int logUVy;
	int thy;

	std::vector<uint8_t> tmpY, tmpU, tmpV;

	int nframes;
	std::unique_ptr<LogoColor[]> logoY, logoU, logoV;

	/*--------------------------------------------------------------------
	*	真中らへんを平均
	*-------------------------------------------------------------------*/
	int med_average(std::vector<uint8_t> s)
	{
		double t = 0;
		int nn = 0;

		int n = (int)s.size();

		// 真中らへんを平均
		for (int i = n / 4; i < n - (n / 4); i++, nn++)
			t += s[i];

		t = (t + nn / 2) / nn;

		return ((int)t);
	}

public:
	// thy: オリジナルだとデフォルト30*8=240（8bitだと12くらい？）
	LogoScan(int scanw, int scanh, int logUVx, int logUVy, int thy)
		: scanw(scanw)
		, scanh(scanh)
		, logUVx(logUVx)
		, logUVy(logUVy)
		, thy(thy)
		, nframes()
	{
	}

	std::unique_ptr<LogoData> GetLogo() const
	{
		int scanUVw = scanw >> logUVx;
		int scanUVh = scanh >> logUVy;
    auto data = std::unique_ptr<LogoData>(new LogoData(scanw, scanh, logUVx, logUVy));
    float *aY = data->GetA(PLANAR_Y);
    float *aU = data->GetA(PLANAR_U);
    float *aV = data->GetA(PLANAR_V);
    float *bY = data->GetB(PLANAR_Y);
    float *bU = data->GetB(PLANAR_U);
    float *bV = data->GetB(PLANAR_V);

		for (int y = 0; y < scanh; ++y) {
			for (int x = 0; x < scanw; ++x) {
				int off = x + y * scanw;
        if (!logoY[off].GetAB(aY[off], bY[off], nframes)) return nullptr;
			}
		}
    for (int y = 0; y < scanUVh; ++y) {
      for (int x = 0; x < scanUVw; ++x) {
        int off = x + y * scanUVw;
        if (!logoU[off].GetAB(aU[off], bU[off], nframes)) return nullptr;
        if (!logoV[off].GetAB(aV[off], bV[off], nframes)) return nullptr;
      }
    }

		return  data;
	}

	void AddScanFrame(
		const uint8_t* srcY, 
		const uint8_t* srcU,
		const uint8_t* srcV,
		int pitchY, int pitchUV,
		int bgY, int bgU, int bgV)
	{
		int scanUVw = scanw >> logUVx;
		int scanUVh = scanh >> logUVy;

		for (int y = 0; y < scanh; ++y) {
			for (int x = 0; x < scanw; ++x) {
				logoY[x + y * scanw].Add(srcY[x + y * pitchY], bgY);
			}
		}
		for (int y = 0; y < scanUVh; ++y) {
			for (int x = 0; x < scanUVw; ++x) {
				logoU[x + y * scanUVw].Add(srcU[x + y * pitchUV], bgU);
				logoV[x + y * scanUVw].Add(srcV[x + y * pitchUV], bgV);
			}
		}

		++nframes;
	}

	bool AddFrame(
		const uint8_t* srcY,
		const uint8_t* srcU,
		const uint8_t* srcV,
		int pitchY, int pitchUV)
	{
		int scanUVw = scanw >> logUVx;
		int scanUVh = scanh >> logUVy;

		tmpY.clear();
		tmpU.clear();
		tmpV.clear();

		tmpY.reserve((scanw + scanh - 1) * 2);
		tmpU.reserve((scanUVw + scanUVh - 1) * 2);
		tmpV.reserve((scanUVw + scanUVh - 1) * 2);

		/*--------------------------------------------------------------------
		*	背景色計算
		*-------------------------------------------------------------------*/

		for (int x = 0; x < scanw; ++x) {
			tmpY.push_back(srcY[x]);
			tmpY.push_back(srcY[x + (scanh - 1) * pitchY]);
		}
		for (int y = 1; y < scanh - 1; ++y) {
			tmpY.push_back(srcY[y * pitchY]);
			tmpY.push_back(srcY[scanw - 1 + y * pitchY]);
		}
		for (int x = 0; x < scanUVw; ++x) {
			tmpU.push_back(srcU[x]);
			tmpU.push_back(srcU[x + (scanUVh - 1) * pitchUV]);
			tmpV.push_back(srcV[x]);
			tmpV.push_back(srcV[x + (scanUVh - 1) * pitchUV]);
		}
		for (int y = 1; y < scanUVh - 1; ++y) {
			tmpU.push_back(srcU[y * pitchY]);
			tmpU.push_back(srcU[scanUVw - 1 + y * pitchUV]);
			tmpV.push_back(srcV[y * pitchY]);
			tmpV.push_back(srcV[scanUVw - 1 + y * pitchUV]);
		}

		// 最小と最大が閾値以上離れている場合、単一色でないと判断
		std::sort(tmpY.begin(), tmpY.end());
		if (abs(tmpY.front() - tmpY.back()) > thy) { // オリジナルだと thy * 8
			return false;
		}
		std::sort(tmpU.begin(), tmpU.end());
		if (abs(tmpU.front() - tmpU.back()) > thy) { // オリジナルだと thy * 8
			return false;
		}
		std::sort(tmpV.begin(), tmpV.end());
		if (abs(tmpV.front() - tmpV.back()) > thy) { // オリジナルだと thy * 8
			return false;
		}

		int bgY = med_average(tmpY);
		int bgU = med_average(tmpU);
		int bgV = med_average(tmpV);

		// 有効フレームを追加
		AddScanFrame(srcY, srcU, srcV, pitchY, pitchUV, bgY, bgU, bgV);

		return true;
	}

	bool AddFrame(PVideoFrame& frame)
	{
		const uint8_t* srcY = reinterpret_cast<const uint8_t*>(frame->GetReadPtr(PLANAR_Y));
		const uint8_t* srcU = reinterpret_cast<const uint8_t*>(frame->GetReadPtr(PLANAR_U));
		const uint8_t* srcV = reinterpret_cast<const uint8_t*>(frame->GetReadPtr(PLANAR_V));
		int pitchY = frame->GetPitch(PLANAR_Y) / sizeof(uint8_t);
		int pitchUV = frame->GetPitch(PLANAR_U) / sizeof(uint8_t);

		return AddFrame(srcY, srcU, srcV, pitchY, pitchUV);
	}
};

class LogoAnalyzer : AMTObject
{
  const TranscoderSetting& setting_;

  int scanw, scanh, thy;
  int logUVx, logUVy;
  int numFrames;
  std::unique_ptr<LogoData> logodata;

  void MakeInitialLogo(int scanx, int scany)
  {
    std::string modulepath = GetModulePath();
    auto env = make_unique_ptr(CreateScriptEnvironment2());
    auto codec = make_unique_ptr(CCodec::CreateInstance(UTVF_ULH0, "Amatsukaze"));

    AVSValue result;
    if (env->LoadPlugin(modulepath.c_str(), false, &result) == false) {
      THROW(RuntimeException, "Failed to LoadPlugin ...");
    }

    PClip clip = env->Invoke("Import", setting_.getFilterScriptPath().c_str(), 0).AsClip();
    VideoInfo vi = clip->GetVideoInfo();

    size_t scanDataSize = scanw * scanh * 3 / 2;
    size_t codedSize = codec->EncodeGetOutputSize(UTVF_YV12, scanw, scanh);
    size_t extraSize = codec->EncodeGetExtraDataSize();
    auto memScanData = std::unique_ptr<uint8_t[]>(new uint8_t[scanDataSize]);
    auto memCoded = std::unique_ptr<uint8_t[]>(new uint8_t[codedSize]);
    std::vector<uint8_t> extra(extraSize);

    if (codec->EncodeGetExtraData(extra.data(), extraSize, UTVF_YV12, scanw, scanh)) {
      THROW(RuntimeException, "failed to EncodeGetExtraData (UtVideo)");
    }
    if (codec->EncodeBegin(UTVF_YV12, scanw, scanh, CBGROSSWIDTH_WINDOWS)) {
      THROW(RuntimeException, "failed to EncodeBegin (UtVideo)");
    }

    logUVx = vi.GetPlaneWidthSubsampling(PLANAR_U);
    logUVy = vi.GetPlaneHeightSubsampling(PLANAR_U);

    LosslessVideoFile file(ctx, setting_.getLogoTmpFilePath(), "wb");
    LogoScan logoscan(scanw, scanh, logUVx, logUVy, thy);

    // フレーム数は最大フレーム数（実際はそこまで書き込まない）
    file.writeHeader(scanw, scanh, vi.num_frames, extra);

    numFrames = 0;
    for (int i = 0; i < vi.num_frames; ++i) {
      PVideoFrame frame = clip->GetFrame(i, env.get());

      // スキャン部分だけ
      int offY = scanx + scany * frame->GetPitch(PLANAR_Y);
      int offUV = (scanx >> logUVx) + (scany >> logUVy) * frame->GetPitch(PLANAR_U);
      PVideoFrame scanFrame = env->SubframePlanar(frame,
        offY, frame->GetPitch(PLANAR_Y), scanw, scanh, offUV, offUV, frame->GetPitch(PLANAR_U));

      if (logoscan.AddFrame(scanFrame)) {
        ++numFrames;

        // 有効なフレームは保存しておく
        CopyYV12(memScanData.get(), frame, scanw, scanh);
        bool keyFrame = false;
        size_t codedSize = codec->EncodeFrame(memCoded.get(), &keyFrame, memScanData.get());
        file.writeFrame(memCoded.get(), (int)codedSize);
      }
    }

    codec->EncodeEnd();

    logodata = logoscan.GetLogo();
    if (logodata == nullptr) {
      THROW(RuntimeException, "Insufficient logo frames");
    }
  }

  double EvaluateLogo()
  {
    // ロゴを評価 //

    // ロゴを除去

    // エッジ評価
  }

  void DeintLogo(LogoData& dst, LogoData& src)
  {
    const float *srcAY = src.GetA(PLANAR_Y);
    float *dstAY = dst.GetA(PLANAR_Y);
    const float *srcBY = src.GetB(PLANAR_Y);
    float *dstBY = dst.GetB(PLANAR_Y);

    auto merge = [](float a, float b, float c) { return (a + 2 * b + c) / 4.0f; };

    for (int y = 1; y < scanh - 1; ++y) {
      for (int x = 0; x < scanw; ++x) {
        dstAY[x + y * scanw] = merge(
          srcAY[x + (y - 1) * scanw],
          srcAY[x + y * scanw],
          srcAY[x + (y + 1) * scanw]);
        dstBY[x + y * scanw] = merge(
          srcBY[x + (y - 1) * scanw],
          srcBY[x + y * scanw],
          srcBY[x + (y + 1) * scanw]);
      }
    }
  }

  void ReMakeLogo()
  {
    // 複数fade値でロゴを評価 //

    // ロゴを評価用にインタレ解除
    LogoData deintLogo(scanw, scanh, logUVx, logUVy);
    DeintLogo(deintLogo, *logodata);

    // 全フレームループ

    // フレームをインタレ解除

    // fade値ループ

    // ロゴを評価

    // 評価値を集約

    // 全フレームループ

    // 評価値の良いフレームだけAddFrame

    // ロゴ作成

  }

public:
  LogoAnalyzer(AMTContext& ctx, const TranscoderSetting& setting)
    : AMTObject(ctx)
    , setting_(setting)
  {
    //
  }

  int ScanLogo()
  {
    int scanx, scany;
    sscanf(setting_.getModeArgs().c_str(), "%d,%d,%d,%d,%d",
      &scanx, &scany, &scanw, &scanh, &thy);

    MakeInitialLogo(scanx, scany);

    // 有効フレームデータと初期ロゴの取得は完了
    // TODO: データ解析とロゴの作り直し

    return 0;
  }
};

int ScanLogo(AMTContext& ctx, const TranscoderSetting& setting)
{
  LogoAnalyzer analyzer(ctx);
  return analyzer.ScanLogo(setting);
}

} // namespace logo
