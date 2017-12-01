/*
* ARIB Caption
* このソースコードはTVCaptionMod2やCaption.dllのコードを流用しています。
* ライセンスはオリジナル版の以下ライセンスに従います。
------引用開始------
●EpgDataCap_Bon、TSEpgView_Sample、NetworkRemocon、Caption、TSEpgViewServerの
ソースの取り扱いについて
　特にGPLとかにはしないので自由に改変してもらって構わないです。
　改変して公開する場合は改変部分のソースぐらいは一緒に公開してください。
　（強制ではないので別に公開しなくてもいいです）
　EpgDataCap.dllの使い方の参考にしてもらうといいかも。

●EpgDataCap.dll、Caption.dllの取り扱いについて
	フリーソフトに組み込む場合は特に制限は設けません。ただし、dllはオリジナルのまま
	組み込んでください。
	このdllを使用したことによって発生した問題について保証は一切行いません。
　商用、シェアウェアなどに組み込むのは不可です。
------引用終了------
*/
#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include <memory>
#include <Wincrypt.h>

#include "CaptionDef.h"
#include "StreamUtils.hpp"
#include "TranscodeSetting.hpp"

inline bool operator==(const CLUT_DAT_DLL &a, const CLUT_DAT_DLL &b) {
	return a.ucR == b.ucR && a.ucG == b.ucG && a.ucB == b.ucB && a.ucAlpha == b.ucAlpha;
}

struct CaptionFormat {

	enum STYLE {
		UNDERLINE = 1,
		SHADOW = 2,
		BOLD = 4,
		ITALIC = 8,

		BOTTOM = 0x10,
		RIGHT = 0x20,
		TOP = 0x40,
		LEFT = 0x80,
	};

	static int GetStyle(const CAPTION_CHAR_DATA_DLL &style) {
		int ret = 0;
		if (style.bUnderLine) ret |= UNDERLINE;
		if (style.bShadow) ret |= SHADOW;
		if (style.bBold) ret |= BOLD;
		if (style.bItalic) ret |= ITALIC;
		ret |= (style.bHLC & 0xF0);
		ret |= (style.bFlushMode & 0x3) << 8;
		return ret;
	}

	int pos;
	float charW, charH;
	float width, height;
	CLUT_DAT_DLL textColor;
	CLUT_DAT_DLL backColor;
	int style;
	int sizeMode;

	bool IsUnderline() const {
		return (style & UNDERLINE) != 0;
	}
	bool IsShadow() const {
		return (style & SHADOW) != 0;
	}
	bool IsBold() const {
		return (style & BOLD) != 0;
	}
	bool IsItalic() const {
		return (style & ITALIC) != 0;
	}
	bool IsHighLightBottom() const {
		return (style & BOTTOM) != 0;
	}
	bool IsHighLightRight() const {
		return (style & RIGHT) != 0;
	}
	bool IsHighLightTop() const {
		return (style & TOP) != 0;
	}
	bool IsHighLightLeft() const {
		return (style & LEFT) != 0;
	}
	int GetFlushMode() const {
		return (style >> 8) & 3;
	}
};

struct CaptionLine {
	std::wstring text;
	int planeW;
	int planeH;
	float posX;
	float posY;
	std::vector<CaptionFormat> formats;

	void Write(const File& file) const {
		std::vector<wchar_t> v(text.begin(), text.end());
		file.writeArray(v);
		file.writeValue(planeW);
		file.writeValue(planeH);
		file.writeValue(posX);
		file.writeValue(posY);
		file.writeArray(formats);
	}

	static std::unique_ptr<CaptionLine> Read(const File& file) {
		auto ptr = std::unique_ptr<CaptionLine>(new CaptionLine());
		auto v = file.readArray<wchar_t>();
		ptr->text.assign(v.begin(), v.end());
		ptr->planeW = file.readValue<int>();
		ptr->planeH = file.readValue<int>();
		ptr->posX = file.readValue<float>();
		ptr->posY = file.readValue<float>();
		ptr->formats = file.readArray<CaptionFormat>();
		return ptr;
	}
};

struct CaptionItem {
	int64_t PTS;
	int langIndex;
	int waitTime;
	// nullだとクリア
	std::unique_ptr<CaptionLine> line;

	void Write(const File& file) const {
		file.writeValue(PTS);
		file.writeValue(langIndex);
		file.writeValue(waitTime);
		if (line) {
			file.writeValue((int)1);
			line->Write(file);
		}
		else {
			file.writeValue((int)0);
		}
	}

	static CaptionItem Read(const File& file) {
		CaptionItem item;
		item.PTS = file.readValue<int64_t>();
		item.langIndex = file.readValue<int>();
		item.waitTime = file.readValue<int>();
		if (file.readValue<int>()) {
			item.line = CaptionLine::Read(file);
		}
		else {
			item.line = nullptr;
		}
		return item;
	}
};

void WriteCaptions(const File& file, const std::vector<CaptionItem>& captions) {
	file.writeValue((int)captions.size());
	for (int i = 0; i < (int)captions.size(); ++i) {
		captions[i].Write(file);
	}
}

std::vector<CaptionItem> ReadCaptions(const File& file) {
	int num = file.readValue<int>();
	std::vector<CaptionItem> ret(num);
	for (int i = 0; i < num; ++i) {
		ret[i] = CaptionItem::Read(file);
	}
	return ret;
}

// 半角置換可能文字リスト
// 記号はJISX0213 1面1区のうちグリフが用意されている可能性が十分高そうなものだけ
static const LPCWSTR HALF_F_LIST = L"　、。，．・：；？！＾＿／｜（［］｛｝「＋－＝＜＞＄％＃＆＊＠０Ａａ";
static const LPCWSTR HALF_T_LIST = L"　、。，．・：；？！＾＿／｜）［］｛｝」＋－＝＜＞＄％＃＆＊＠９Ｚｚ";
static const LPCWSTR HALF_R_LIST = L" ､｡,.･:;?!^_/|([]{}｢+-=<>$%#&*@0Aa";

static BOOL CalcMD5FromDRCSPattern(std::vector<char>& hash, const DRCS_PATTERN_DLL *pPattern)
{
	WORD wGradation = pPattern->wGradation;
	int nWidth = pPattern->bmiHeader.biWidth;
	int nHeight = pPattern->bmiHeader.biHeight;
	if (!(wGradation == 2 || wGradation == 4) || nWidth>DRCS_SIZE_MAX || nHeight>DRCS_SIZE_MAX){
		return FALSE;
	}
	BYTE bData[(DRCS_SIZE_MAX*DRCS_SIZE_MAX + 3) / 4] = {};
	const BYTE *pbBitmap = pPattern->pbBitmap;

	DWORD dwDataLen = wGradation == 2 ? (nWidth*nHeight + 7) / 8 : (nWidth*nHeight + 3) / 4;
	DWORD dwSizeImage = 0;
	for (int y = nHeight - 1; y >= 0; y--){
		for (int x = 0; x<nWidth; x++){
			int nPix = x % 2 == 0 ? pbBitmap[dwSizeImage++] >> 4 :
				pbBitmap[dwSizeImage - 1] & 0x0F;
			int nPos = y*nWidth + x;
			if (wGradation == 2){
				bData[nPos / 8] |= (BYTE)((nPix / 3) << (7 - nPos % 8));
			}
			else{
				bData[nPos / 4] |= (BYTE)(nPix << ((3 - nPos % 4) * 2));
			}
		}
		dwSizeImage = (dwSizeImage + 3) / 4 * 4;
	}

	HCRYPTPROV hProv = NULL;
	HCRYPTHASH hHash = NULL;
	BOOL bRet = FALSE;
	if (!::CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)){
		hProv = NULL;
		goto EXIT;
	}
	if (!::CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash)){
		hHash = NULL;
		goto EXIT;
	}
	if (!::CryptHashData(hHash, bData, dwDataLen, 0)) goto EXIT;
	DWORD dwHashLen = 16;
	BYTE bHash[16];
	if (!::CryptGetHashParam(hHash, HP_HASHVAL, bHash, &dwHashLen, 0)) goto EXIT;

	static const char* digits = "0123456789ABCDEF";
	hash.resize(32);
	for (int i = 0; i < 16; ++i) {
		hash[i * 2 + 0] = digits[bHash[i] & 0x0F];
		hash[i * 2 + 1] = digits[bHash[i] >> 4];
	}

	bRet = TRUE;
EXIT:
	if (hHash) ::CryptDestroyHash(hHash);
	if (hProv) ::CryptReleaseContext(hProv, 0);
	return bRet;
}

static void SaveDRCSImage(const std::string& filename, const DRCS_PATTERN_DLL* pData)
{
	//ファイルがなければ書きこむ
	if (File::exists(filename) == false) {
		//どんな配色にしても構わない。colors[4]以上の色は出現しない
		RGBQUAD colors[16] = { { 255, 255, 255, 0 },{ 170, 170, 170, 0 },{ 85, 85, 85, 0 },{ 0, 0, 0, 0 } };
		BITMAPFILEHEADER bmfHeader = { 0 };
		bmfHeader.bfType = 0x4D42;
		bmfHeader.bfOffBits = sizeof(bmfHeader) + sizeof(pData->bmiHeader) + sizeof(colors);
		bmfHeader.bfSize = bmfHeader.bfOffBits + pData->bmiHeader.biSizeImage;

		File file(filename, "wb");
		file.writeValue(bmfHeader);
		file.writeValue(pData->bmiHeader);
		file.write(MemoryChunk((uint8_t*)colors, sizeof(colors)));
		file.write(MemoryChunk((uint8_t*)pData->pbBitmap, pData->bmiHeader.biSizeImage));
	}
}

static int StrlenWoLoSurrogate(LPCWSTR str)
{
	int len = 0;
	for (; *str; ++str) {
		if ((*str & 0xFC00) != 0xDC00) ++len;
	}
	return len;
}

class CaptionDLLParser : public AMTObject
{
public:
	CaptionDLLParser(AMTContext& ctx)
		: AMTObject(ctx)
	{ }

	// 最初の１つだけ処理する
	CaptionItem ProcessCaption(int64_t PTS, int langIndex,
		const CAPTION_DATA_DLL* capList, int capCount, DRCS_PATTERN_DLL* pDrcsList, int drcsCount)
	{
		const CAPTION_DATA_DLL& caption = capList[0];

		CaptionItem item;
		item.PTS = PTS;
		item.langIndex = langIndex;
		item.waitTime = caption.dwWaitTime;

		if (caption.bClear) {
		}
		else {
			item.line = ShowCaptionData(caption, pDrcsList, drcsCount);
		}

		return item;
	}

	virtual std::string getDRCSOutPath(const std::string& md5) = 0;

private:

	// 拡縮後の文字サイズを得る
	static void GetCharSize(float *pCharW, float *pCharH, float *pDirW, float *pDirH, const CAPTION_CHAR_DATA_DLL &charData)
	{
		float charTransX = 2;
		float charTransY = 2;
		switch (charData.wCharSizeMode) {
		case CP_STR_SMALL:
			charTransX = 1;
			charTransY = 1;
			break;
		case CP_STR_MEDIUM:
			charTransX = 1;
			charTransY = 2;
			break;
		case CP_STR_HIGH_W:
			charTransX = 2;
			charTransY = 4;
			break;
		case CP_STR_WIDTH_W:
			charTransX = 4;
			charTransY = 2;
			break;
		case CP_STR_W:
			charTransX = 4;
			charTransY = 4;
			break;
		}
		if (pCharW) *pCharW = charData.wCharW * charTransX / 2;
		if (pCharH) *pCharH = charData.wCharH * charTransY / 2;
		if (pDirW) *pDirW = (charData.wCharW + charData.wCharHInterval) * charTransX / 2;
		if (pDirH) *pDirH = (charData.wCharH + charData.wCharVInterval) * charTransY / 2;
	}

	void AddText(CaptionLine& line, const std::wstring& text,
		float charW, float charH, float width, float height, const CAPTION_CHAR_DATA_DLL &style)
	{
		line.formats.emplace_back();

		CaptionFormat& fmt = line.formats.back();
		fmt.pos = (int)line.text.size();
		fmt.charW = charW;
		fmt.charH = charH;
		fmt.width = width;
		fmt.height = height;
		fmt.textColor = style.stCharColor;
		fmt.backColor = style.stBackColor;
		fmt.style = CaptionFormat::GetStyle(style);
		fmt.sizeMode = style.wCharSizeMode;

		line.text += text;
	}

	// 字幕本文を1行だけ処理する
	std::unique_ptr<CaptionLine> ShowCaptionData(
		const CAPTION_DATA_DLL &caption, const DRCS_PATTERN_DLL *pDrcsList, DWORD drcsCount)
	{
		auto line = std::unique_ptr<CaptionLine>(new CaptionLine());

		if (caption.wSWFMode == 9 || caption.wSWFMode == 10) {
			line->planeW = 720;
			line->planeH = 480;
		}
		else {
			line->planeW = 960;
			line->planeH = 540;
		}
		line->posX = caption.wPosX;
		line->posY = caption.wPosY;

		for (DWORD i = 0; i < caption.dwListCount; ++i) {
			const CAPTION_CHAR_DATA_DLL &charData = caption.pstCharList[i];

			float charW, charH, dirW, dirH;
			GetCharSize(&charW, &charH, &dirW, &dirH, charData);

			bool fSearchHalf = (charData.wCharSizeMode == CP_STR_MEDIUM);

			std::wstring srctext = static_cast<LPCWSTR>(charData.pszDecode);
			while (srctext.size() > 0) {
				std::wstring showtext = srctext;
				std::wstring nexttext;

				// 文字列にDRCSか外字か半角置換可能文字が含まれるか調べる
				const DRCS_PATTERN_DLL *pDrcs = NULL;
				LPCWSTR pszDrcsStr = NULL;
				WCHAR szHalf[2] = {};
				if (drcsCount != 0 || fSearchHalf) {
					for (int j = 0; j < (int)srctext.size(); ++j) {
						if (0xEC00 <= srctext[j] && srctext[j] <= 0xECFF) {
							// DRCS
							for (DWORD k = 0; k < drcsCount; ++k) {
								if (pDrcsList[k].dwUCS == srctext[j]) {
									pDrcs = &pDrcsList[k];
									if (pDrcsList[k].bmiHeader.biWidth == charW &&
										pDrcsList[k].bmiHeader.biHeight == charH)
									{
										break;
									}
								}
							}
							if (pDrcs) {
								// もしあれば置きかえ可能な文字列を取得
								std::vector<char> md5;
								if (CalcMD5FromDRCSPattern(md5, pDrcs)) {
									std::string md5str(md5.begin(), md5.end());
									auto& drcsmap = ctx.getDRCSMapping();
									auto it = drcsmap.find(md5str);
									if (it != drcsmap.end()) {
										pszDrcsStr = it->second.c_str();
									}
									else {
										// マッピングがないので画像を保存する
										auto filename = getDRCSOutPath(std::string(md5.begin(), md5.end()));
										SaveDRCSImage(filename, pDrcs);

										ctx.incrementCounter("drcsnomap");
										ctx.warn("[字幕] マッピングのないDRCS外字があります。追加してください -> %s", filename.c_str());
									}
								}
								showtext = srctext.substr(0, j);
								nexttext = srctext.substr(j + 1);
								break;
							}
						}
						else if (fSearchHalf) {
							for (int k = 0; HALF_F_LIST[k]; ++k) {
								wchar_t r = HALF_R_LIST[k];
								if ((r != L'A' && r != L'a') &&
									(r != L'0') &&
									(r == L'A' || r == L'a' || r == L'0') &&
									HALF_F_LIST[k] <= srctext[j] && srctext[j] <= HALF_T_LIST[k])
								{
									// 半角置換可能文字
									szHalf[0] = r + srctext[j] - HALF_F_LIST[k];
									szHalf[1] = 0;
									showtext = srctext.substr(0, j);
									nexttext = srctext.substr(j + 1);
									break;
								}
							}
							if (nexttext.size()) break;
						}
					}
				}

				// 文字列を描画
				int lenWos = StrlenWoLoSurrogate(showtext.c_str());
				if (showtext.size() > 0) {
					AddText(*line, showtext, charW, charH, dirW * lenWos, dirH, charData);
				}

				if (pDrcs) {
					// DRCSを文字列で描画
					if (pszDrcsStr == nullptr) {
						pszDrcsStr = L"□";
					}
					lenWos = StrlenWoLoSurrogate(pszDrcsStr);
					if (lenWos > 0) {
						// レイアウト維持のため、何文字であっても1文字幅に詰める
						AddText(*line, pszDrcsStr, (float)charData.wCharW / lenWos, charH, dirW, dirH, charData);
					}
				}
				else if (szHalf[0]) {
					// 半角文字を描画
					AddText(*line, szHalf, charW, charH, dirW, dirH, charData);
				}

				srctext = nexttext;
			}
		}

		return line;
	}
};


