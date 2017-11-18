/*
* ARIB String
* このソースコードは大部分がTvTestのコードです。
* https://github.com/DBCTRADO/TVTest
* GPLで利用しています。
*/
#pragma once

namespace aribstring {

// 他の部分は全部設定なし（Ansi）だが、とりあえずこのファイルだけUnicodeを使うようにする

#define _T(str) L ## str

#ifndef _UNICODE
#define __HACK_UNICODE__
#define _UNICODE
#endif

typedef const wchar_t* LPCTSTR;
typedef wchar_t TCHAR;

int lstrlen(LPCTSTR lpString) { return lstrlenW(lpString); }

static const bool abCharSizeTable[] =
{
	false,	// CODE_UNKNOWN					不明なグラフィックセット(非対応)
	true,	// CODE_KANJI					Kanji
	false,	// CODE_ALPHANUMERIC			Alphanumeric
	false,	// CODE_HIRAGANA				Hiragana
	false,	// CODE_KATAKANA				Katakana
	false,	// CODE_MOSAIC_A				Mosaic A
	false,	// CODE_MOSAIC_B				Mosaic B
	false,	// CODE_MOSAIC_C				Mosaic C
	false,	// CODE_MOSAIC_D				Mosaic D
	false,	// CODE_PROP_ALPHANUMERIC		Proportional Alphanumeric
	false,	// CODE_PROP_HIRAGANA			Proportional Hiragana
	false,	// CODE_PROP_KATAKANA			Proportional Katakana
	false,	// CODE_JIS_X0201_KATAKANA		JIS X 0201 Katakana
	true,	// CODE_JIS_KANJI_PLANE_1		JIS compatible Kanji Plane 1
	true,	// CODE_JIS_KANJI_PLANE_2		JIS compatible Kanji Plane 2
	true,	// CODE_ADDITIONAL_SYMBOLS		Additional symbols
	true,	// CODE_DRCS_0					DRCS-0
	false,	// CODE_DRCS_1					DRCS-1
	false,	// CODE_DRCS_2					DRCS-2
	false,	// CODE_DRCS_3					DRCS-3
	false,	// CODE_DRCS_4					DRCS-4
	false,	// CODE_DRCS_5					DRCS-5
	false,	// CODE_DRCS_6					DRCS-6
	false,	// CODE_DRCS_7					DRCS-7
	false,	// CODE_DRCS_8					DRCS-8
	false,	// CODE_DRCS_9					DRCS-9
	false,	// CODE_DRCS_10					DRCS-10
	false,	// CODE_DRCS_11					DRCS-11
	false,	// CODE_DRCS_12					DRCS-12
	false,	// CODE_DRCS_13					DRCS-13
	false,	// CODE_DRCS_14					DRCS-14
	false,	// CODE_DRCS_15					DRCS-15
	false,	// CODE_MACRO					Macro
};

class CAribString
{
public:
	enum CHAR_SIZE {
		SIZE_SMALL,		// 小型
		SIZE_MEDIUM,	// 中型
		SIZE_NORMAL,	// 標準
		SIZE_MICRO,		// 超小型
		SIZE_HIGH_W,	// 縦倍
		SIZE_WIDTH_W,	// 横倍
		SIZE_W,			// 縦横倍
		SIZE_SPECIAL_1,	// 特殊1
		SIZE_SPECIAL_2	// 特殊2
	};

	struct FormatInfo {
		DWORD Pos;
		CHAR_SIZE Size;
		BYTE CharColorIndex;
		BYTE BackColorIndex;
		BYTE RasterColorIndex;
		bool operator==(const FormatInfo &o) {
			return Pos == o.Pos
				&& Size == o.Size
				&& CharColorIndex == o.CharColorIndex
				&& BackColorIndex == o.BackColorIndex
				&& RasterColorIndex == o.RasterColorIndex;
		}
		bool operator!=(const FormatInfo &o) { return !(*this == o); }
	};
	typedef std::vector<FormatInfo> FormatList;

	class __declspec(novtable) IDRCSMap {
	public:
		virtual ~IDRCSMap() {}
		virtual LPCTSTR GetString(WORD Code) = 0;
	};

	static const DWORD AribToString(TCHAR *lpszDst, const DWORD dwDstLen, const BYTE *pSrcData, const DWORD dwSrcLen)
	{
		// ARIB STD-B24 Part1 → Shift-JIS / Unicode変換
		CAribString WorkObject;

		return WorkObject.AribToStringInternal(lpszDst, dwDstLen, pSrcData, dwSrcLen);
	}
	static const DWORD CaptionToString(TCHAR *lpszDst, const DWORD dwDstLen, const BYTE *pSrcData, const DWORD dwSrcLen, FormatList *pFormatList = NULL, IDRCSMap *pDRCSMap = NULL)
	{
		CAribString WorkObject;

		return WorkObject.AribToStringInternal(lpszDst, dwDstLen, pSrcData, dwSrcLen, true, pFormatList, pDRCSMap);
	}

private:
	enum CODE_SET
	{
		CODE_UNKNOWN,				// 不明なグラフィックセット(非対応)
		CODE_KANJI,					// Kanji
		CODE_ALPHANUMERIC,			// Alphanumeric
		CODE_HIRAGANA,				// Hiragana
		CODE_KATAKANA,				// Katakana
		CODE_MOSAIC_A,				// Mosaic A
		CODE_MOSAIC_B,				// Mosaic B
		CODE_MOSAIC_C,				// Mosaic C
		CODE_MOSAIC_D,				// Mosaic D
		CODE_PROP_ALPHANUMERIC,		// Proportional Alphanumeric
		CODE_PROP_HIRAGANA,			// Proportional Hiragana
		CODE_PROP_KATAKANA,			// Proportional Katakana
		CODE_JIS_X0201_KATAKANA,	// JIS X 0201 Katakana
		CODE_JIS_KANJI_PLANE_1,		// JIS compatible Kanji Plane 1
		CODE_JIS_KANJI_PLANE_2,		// JIS compatible Kanji Plane 2
		CODE_ADDITIONAL_SYMBOLS,	// Additional symbols
		CODE_DRCS_0,				// DRCS-0
		CODE_DRCS_1,				// DRCS-1
		CODE_DRCS_2,				// DRCS-2
		CODE_DRCS_3,				// DRCS-3
		CODE_DRCS_4,				// DRCS-4
		CODE_DRCS_5,				// DRCS-5
		CODE_DRCS_6,				// DRCS-6
		CODE_DRCS_7,				// DRCS-7
		CODE_DRCS_8,				// DRCS-8
		CODE_DRCS_9,				// DRCS-9
		CODE_DRCS_10,				// DRCS-10
		CODE_DRCS_11,				// DRCS-11
		CODE_DRCS_12,				// DRCS-12
		CODE_DRCS_13,				// DRCS-13
		CODE_DRCS_14,				// DRCS-14
		CODE_DRCS_15,				// DRCS-15
		CODE_MACRO					// Macro
	};

	CODE_SET m_CodeG[4];
	CODE_SET *m_pLockingGL;
	CODE_SET *m_pLockingGR;
	CODE_SET *m_pSingleGL;

	BYTE m_byEscSeqCount;
	BYTE m_byEscSeqIndex;
	bool m_bIsEscSeqDrcs;

	CHAR_SIZE m_CharSize;
	BYTE m_CharColorIndex;
	BYTE m_BackColorIndex;
	BYTE m_RasterColorIndex;
	BYTE m_DefPalette;
	BYTE m_RPC;
	FormatList *m_pFormatList;
	IDRCSMap *m_pDRCSMap;

	bool m_bCaption;

	const DWORD AribToStringInternal(TCHAR *lpszDst, const DWORD dwDstLen, const BYTE *pSrcData, const DWORD dwSrcLen,
		const bool bCaption = false, FormatList *pFormatList = NULL, IDRCSMap *pDRCSMap = NULL)
	{
		if (pSrcData == NULL || lpszDst == NULL || dwDstLen == 0)
			return 0UL;

		// 状態初期設定
		m_byEscSeqCount = 0U;
		m_pSingleGL = NULL;

		m_CodeG[0] = CODE_KANJI;
		m_CodeG[1] = CODE_ALPHANUMERIC;
		m_CodeG[2] = CODE_HIRAGANA;
		m_CodeG[3] = bCaption ? CODE_MACRO : CODE_KATAKANA;

		m_pLockingGL = &m_CodeG[0];
		m_pLockingGR = &m_CodeG[2];
#ifdef BONTSENGINE_1SEG_SUPPORT
		if (bCaption) {
			m_CodeG[1] = CODE_DRCS_1;
			m_pLockingGL = &m_CodeG[1];
			m_pLockingGR = &m_CodeG[0];
		}
#endif

		m_CharSize = SIZE_NORMAL;
		if (bCaption) {
			m_CharColorIndex = 7;
			m_BackColorIndex = 8;
			m_RasterColorIndex = 8;
		}
		else {
			m_CharColorIndex = 0;
			m_BackColorIndex = 0;
			m_RasterColorIndex = 0;
		}
		m_DefPalette = 0;
		m_RPC = 1;

		m_bCaption = bCaption;
		m_pFormatList = pFormatList;
		m_pDRCSMap = pDRCSMap;

		return ProcessString(lpszDst, dwDstLen, pSrcData, dwSrcLen);
	}

	const DWORD ProcessString(TCHAR *lpszDst, const DWORD dwDstLen, const BYTE *pSrcData, const DWORD dwSrcLen)
	{
		DWORD dwSrcPos = 0UL, dwDstPos = 0UL;
		int Length;

		while (dwSrcPos < dwSrcLen && dwDstPos < dwDstLen - 1) {
			if (!m_byEscSeqCount) {
				// GL/GR領域
				if ((pSrcData[dwSrcPos] >= 0x21U) && (pSrcData[dwSrcPos] <= 0x7EU)) {
					// GL領域
					const CODE_SET CurCodeSet = (m_pSingleGL) ? *m_pSingleGL : *m_pLockingGL;
					m_pSingleGL = NULL;

					if (abCharSizeTable[CurCodeSet]) {
						// 2バイトコード
						if ((dwSrcLen - dwSrcPos) < 2UL)
							break;

						Length = ProcessCharCode(&lpszDst[dwDstPos], dwDstLen - dwDstPos - 1, ((WORD)pSrcData[dwSrcPos + 0] << 8) | (WORD)pSrcData[dwSrcPos + 1], CurCodeSet);
						if (Length < 0)
							break;
						dwDstPos += Length;
						dwSrcPos++;
					}
					else {
						// 1バイトコード
						Length = ProcessCharCode(&lpszDst[dwDstPos], dwDstLen - dwDstPos - 1, (WORD)pSrcData[dwSrcPos], CurCodeSet);
						if (Length < 0)
							break;
						dwDstPos += Length;
					}
				}
				else if ((pSrcData[dwSrcPos] >= 0xA1U) && (pSrcData[dwSrcPos] <= 0xFEU)) {
					// GR領域
					const CODE_SET CurCodeSet = *m_pLockingGR;

					if (abCharSizeTable[CurCodeSet]) {
						// 2バイトコード
						if ((dwSrcLen - dwSrcPos) < 2UL) break;

						Length = ProcessCharCode(&lpszDst[dwDstPos], dwDstLen - dwDstPos - 1, ((WORD)(pSrcData[dwSrcPos + 0] & 0x7FU) << 8) | (WORD)(pSrcData[dwSrcPos + 1] & 0x7FU), CurCodeSet);
						if (Length < 0)
							break;
						dwDstPos += Length;
						dwSrcPos++;
					}
					else {
						// 1バイトコード
						Length = ProcessCharCode(&lpszDst[dwDstPos], dwDstLen - dwDstPos - 1, (WORD)(pSrcData[dwSrcPos] & 0x7FU), CurCodeSet);
						if (Length < 0)
							break;
						dwDstPos += Length;
					}
				}
				else {
					// 制御コード
					switch (pSrcData[dwSrcPos]) {
					case 0x0D:	// APR
						lpszDst[dwDstPos++] = '\r';
						if (dwDstPos + 1 < dwDstLen)
							lpszDst[dwDstPos++] = '\n';
						break;
					case 0x0F:	LockingShiftGL(0U);					break;	// LS0
					case 0x0E:	LockingShiftGL(1U);					break;	// LS1
					case 0x19:	SingleShiftGL(2U);					break;	// SS2
					case 0x1D:	SingleShiftGL(3U);					break;	// SS3
					case 0x1B:	m_byEscSeqCount = 1U;				break;	// ESC
					case 0x20:	// SP
						if (IsSmallCharMode()) {
							lpszDst[dwDstPos++] = _T(' ');
						}
						else {
#ifdef _UNICODE
							lpszDst[dwDstPos++] = L'　';
#else
							if (dwDstPos + 2 < dwDstLen) {
								lpszDst[dwDstPos++] = "　"[0];
								lpszDst[dwDstPos++] = "　"[1];
							}
							else {
								lpszDst[dwDstPos++] = ' ';
							}
#endif
						}
						break;
					case 0xA0:	lpszDst[dwDstPos++] = _T(' ');	break;	// SP

					case 0x80:
					case 0x81:
					case 0x82:
					case 0x83:
					case 0x84:
					case 0x85:
					case 0x86:
					case 0x87:
						m_CharColorIndex = (m_DefPalette << 4) | (pSrcData[dwSrcPos] & 0x0F);
						SetFormat(dwDstPos);
						break;

					case 0x88:	// SSZ 小型
						m_CharSize = SIZE_SMALL;
						SetFormat(dwDstPos);
						break;
					case 0x89:	// MSZ 中型
						m_CharSize = SIZE_MEDIUM;
						SetFormat(dwDstPos);
						break;
					case 0x8A:	// NSZ 標準
						m_CharSize = SIZE_NORMAL;
						SetFormat(dwDstPos);
						break;
					case 0x8B:	// SZX 指定サイズ
						if (++dwSrcPos >= dwSrcLen)
							break;
						switch (pSrcData[dwSrcPos]) {
						case 0x60:	m_CharSize = SIZE_MICRO;		break;	// 超小型
						case 0x41:	m_CharSize = SIZE_HIGH_W;		break;	// 縦倍
						case 0x44:	m_CharSize = SIZE_WIDTH_W;		break;	// 横倍
						case 0x45:	m_CharSize = SIZE_W;			break;	// 縦横倍
						case 0x6B:	m_CharSize = SIZE_SPECIAL_1;	break;	// 特殊1
						case 0x64:	m_CharSize = SIZE_SPECIAL_2;	break;	// 特殊2
						}
						SetFormat(dwDstPos);
						break;

					case 0x0C: //CS
						lpszDst[dwDstPos++] = '\f';
						break;
					case 0x16:	dwSrcPos++;		break;	// PAPF
					case 0x1C:	dwSrcPos += 2;	break;	// APS
					case 0x90:	// COL
						if (++dwSrcPos >= dwSrcLen)
							break;
						if (pSrcData[dwSrcPos] == 0x20) {
							m_DefPalette = pSrcData[++dwSrcPos] & 0x0F;
						}
						else {
							switch (pSrcData[dwSrcPos] & 0xF0) {
							case 0x40:
								m_CharColorIndex = pSrcData[dwSrcPos] & 0x0F;
								break;
							case 0x50:
								m_BackColorIndex = pSrcData[dwSrcPos] & 0x0F;
								break;
							}
							SetFormat(dwDstPos);
						}
						break;
					case 0x91:	dwSrcPos++;		break;	// FLC
					case 0x93:	dwSrcPos++;		break;	// POL
					case 0x94:	dwSrcPos++;		break;	// WMM
					case 0x95:	// MACRO
						do {
							if (++dwSrcPos >= dwSrcLen)
								break;
						} while (pSrcData[dwSrcPos] != 0x4F);
						break;
					case 0x97:	dwSrcPos++;		break;	// HLC
					case 0x98:	// RPC
						if (++dwSrcPos >= dwSrcLen)
							break;
						m_RPC = pSrcData[dwSrcPos] & 0x3F;
						break;
					case 0x9B:	//CSI
						for (Length = 0; ++dwSrcPos < dwSrcLen && pSrcData[dwSrcPos] <= 0x3B; Length++);
						if (dwSrcPos < dwSrcLen) {
							if (pSrcData[dwSrcPos] == 0x69) {	// ACS
								if (Length != 2)
									goto End;
								if (pSrcData[dwSrcPos - 2] >= 0x32) {
									while (++dwSrcPos < dwSrcLen && pSrcData[dwSrcPos] != 0x9B);
									dwSrcPos += 3;
								}
							}
						}
						break;
					case 0x9D:	// TIME
						if (++dwSrcPos >= dwSrcLen)
							break;
						if (pSrcData[dwSrcPos] == 0x20) {
							dwSrcPos++;
						}
						else {
							while (pSrcData[dwSrcPos] < 0x40 || pSrcData[dwSrcPos] > 0x43)
								dwSrcPos++;
						}
						break;
					default:	break;	// 非対応
					}
				}
			}
			else {
				// エスケープシーケンス処理
				ProcessEscapeSeq(pSrcData[dwSrcPos]);
			}

			dwSrcPos++;
		}

	End:
		// 終端文字
		lpszDst[dwDstPos] = _T('\0');

		return dwDstPos;
	}

	inline const int ProcessCharCode(TCHAR *lpszDst, const DWORD dwDstLen, const WORD wCode, const CODE_SET CodeSet)
	{
		int Length;

		switch (CodeSet) {
		case CODE_KANJI:
		case CODE_JIS_KANJI_PLANE_1:
		case CODE_JIS_KANJI_PLANE_2:
			// 漢字コード出力
			Length = PutKanjiChar(lpszDst, dwDstLen, wCode);
			break;

		case CODE_ALPHANUMERIC:
		case CODE_PROP_ALPHANUMERIC:
			// 英数字コード出力
			Length = PutAlphanumericChar(lpszDst, dwDstLen, wCode);
			break;

		case CODE_HIRAGANA:
		case CODE_PROP_HIRAGANA:
			// ひらがなコード出力
			Length = PutHiraganaChar(lpszDst, dwDstLen, wCode);
			break;

		case CODE_PROP_KATAKANA:
		case CODE_KATAKANA:
			// カタカナコード出力
			Length = PutKatakanaChar(lpszDst, dwDstLen, wCode);
			break;

		case CODE_JIS_X0201_KATAKANA:
			// JISカタカナコード出力
			Length = PutJisKatakanaChar(lpszDst, dwDstLen, wCode);
			break;

		case CODE_ADDITIONAL_SYMBOLS:
			// 追加シンボルコード出力
			Length = PutSymbolsChar(lpszDst, dwDstLen, wCode);
			break;

		case CODE_MACRO:
			Length = PutMacroChar(lpszDst, dwDstLen, wCode);
			break;

		case CODE_DRCS_0:
			Length = PutDRCSChar(lpszDst, dwDstLen, wCode);
			break;

		case CODE_DRCS_1:
		case CODE_DRCS_2:
		case CODE_DRCS_3:
		case CODE_DRCS_4:
		case CODE_DRCS_5:
		case CODE_DRCS_6:
		case CODE_DRCS_7:
		case CODE_DRCS_8:
		case CODE_DRCS_9:
		case CODE_DRCS_10:
		case CODE_DRCS_11:
		case CODE_DRCS_12:
		case CODE_DRCS_13:
		case CODE_DRCS_14:
		case CODE_DRCS_15:
			Length = PutDRCSChar(lpszDst, dwDstLen, ((CodeSet - CODE_DRCS_0 + 0x40) << 8) | wCode);
			break;

		default:
#ifdef _UNICODE
			lpszDst[0] = L'□';
			Length = 1;
#else
			if (dwDstLen < 2)
				return -1;
			lpszDst[0] = "□"[0];
			lpszDst[1] = "□"[1];
			Length = 2;
#endif
		}

		if (m_RPC > 1 && Length == 1 && dwDstLen > 1) {
			DWORD Count = std::min((DWORD)m_RPC - 1, dwDstLen - Length);
			while (Count-- > 0)
				lpszDst[Length++] = lpszDst[0];
		}
		m_RPC = 1;

		return Length;
	}

	inline const int PutKanjiChar(TCHAR *lpszDst, const DWORD dwDstLen, const WORD wCode)
	{
		if (wCode >= 0x7521)
			return PutSymbolsChar(lpszDst, dwDstLen, wCode);

		// JIS → Shift_JIS漢字コード変換
		BYTE First = (BYTE)(wCode >> 8), Second = (BYTE)(wCode & 0x00FF);
		First -= 0x21;
		if ((First & 0x01) == 0) {
			Second += 0x1F;
			if (Second >= 0x7F)
				Second++;
		}
		else {
			Second += 0x7E;
		}
		First >>= 1;
		if (First >= 0x1F)
			First += 0xC1;
		else
			First += 0x81;

#ifdef _UNICODE
		// Shift_JIS → UNICODE
		if (dwDstLen < 1)
			return -1;
		char cShiftJIS[2];
		cShiftJIS[0] = (char)First;
		cShiftJIS[1] = (char)Second;
		// Shift_JIS = Code page 932
		int Length = ::MultiByteToWideChar(932, MB_PRECOMPOSED, cShiftJIS, 2, lpszDst, dwDstLen);
		if (Length == 0) {
			lpszDst[0] = L'□';
			return 1;
		}
		return Length;
#else
		// Shift_JIS → Shift_JIS
		if (dwDstLen < 2)
			return -1;
		lpszDst[0] = (char)First;
		lpszDst[1] = (char)Second;
		return 2;
#endif
	}

	inline const int PutAlphanumericChar(TCHAR *lpszDst, const DWORD dwDstLen, const WORD wCode)
	{
		// 英数字文字コード変換
		static const LPCTSTR acAlphanumericTable =
			_T("　　　　　　　　　　　　　　　　")
			_T("　　　　　　　　　　　　　　　　")
			_T("　！”＃＄％＆’（）＊＋，－．／")
			_T("０１２３４５６７８９：；＜＝＞？")
			_T("＠ＡＢＣＤＥＦＧＨＩＪＫＬＭＮＯ")
			_T("ＰＱＲＳＴＵＶＷＸＹＺ［￥］＾＿")
			_T("　ａｂｃｄｅｆｇｈｉｊｋｌｍｎｏ")
			_T("ｐｑｒｓｔｕｖｗｘｙｚ｛｜｝￣　");

#ifdef _UNICODE
		if (dwDstLen < 1)
			return -1;
		lpszDst[0] = acAlphanumericTable[wCode];

		return 1;
#else
		if (dwDstLen < 2)
			return -1;
		lpszDst[0] = acAlphanumericTable[wCode * 2U + 0U];
		lpszDst[1] = acAlphanumericTable[wCode * 2U + 1U];

		return 2;
#endif
	}

	inline const int PutHiraganaChar(TCHAR *lpszDst, const DWORD dwDstLen, const WORD wCode)
	{
		// ひらがな文字コード変換
		static const LPCTSTR acHiraganaTable =
			_T("　　　　　　　　　　　　　　　　")
			_T("　　　　　　　　　　　　　　　　")
			_T("　ぁあぃいぅうぇえぉおかがきぎく")
			_T("ぐけげこごさざしじすずせぜそぞた")
			_T("だちぢっつづてでとどなにぬねのは")
			_T("ばぱひびぴふぶぷへべぺほぼぽまみ")
			_T("むめもゃやゅゆょよらりるれろゎわ")
			_T("ゐゑをん　　　ゝゞー。「」、・　");

#ifdef _UNICODE
		if (dwDstLen < 1)
			return -1;
		lpszDst[0] = acHiraganaTable[wCode];

		return 1;
#else
		if (dwDstLen < 2)
			return -1;
		lpszDst[0] = acHiraganaTable[wCode * 2U + 0U];
		lpszDst[1] = acHiraganaTable[wCode * 2U + 1U];

		return 2;
#endif
	}

	inline const int PutKatakanaChar(TCHAR *lpszDst, const DWORD dwDstLen, const WORD wCode)
	{
		// カタカナ文字コード変換
		static const LPCTSTR acKatakanaTable =
			_T("　　　　　　　　　　　　　　　　")
			_T("　　　　　　　　　　　　　　　　")
			_T("　ァアィイゥウェエォオカガキギク")
			_T("グケゲコゴサザシジスズセゼソゾタ")
			_T("ダチヂッツヅテデトドナニヌネノハ")
			_T("バパヒビピフブプヘベペホボポマミ")
			_T("ムメモャヤュユョヨラリルレロヮワ")
			_T("ヰヱヲンヴヵヶヽヾー。「」、・　");

#ifdef _UNICODE
		if (dwDstLen < 1)
			return -1;
		lpszDst[0] = acKatakanaTable[wCode];

		return 1;
#else
		if (dwDstLen < 2)
			return -1;
		lpszDst[0] = acKatakanaTable[wCode * 2U + 0U];
		lpszDst[1] = acKatakanaTable[wCode * 2U + 1U];

		return 2;
#endif
	}

	inline const int PutJisKatakanaChar(TCHAR *lpszDst, const DWORD dwDstLen, const WORD wCode)
	{
		// JISカタカナ文字コード変換
		static const LPCTSTR acJisKatakanaTable =
			_T("　　　　　　　　　　　　　　　　")
			_T("　　　　　　　　　　　　　　　　")
			_T("　。「」、・ヲァィゥェォャュョッ")
			_T("ーアイウエオカキクケコサシスセソ")
			_T("タチツテトナニヌネノハヒフヘホマ")
			_T("ミムメモヤユヨラリルレロワン゛゜")
			_T("　　　　　　　　　　　　　　　　")
			_T("　　　　　　　　　　　　　　　　");

#ifdef _UNICODE
		if (dwDstLen < 1)
			return -1;
		lpszDst[0] = acJisKatakanaTable[wCode];

		return 1;
#else
		if (dwDstLen < 2)
			return -1;
		lpszDst[0] = acJisKatakanaTable[wCode * 2U + 0U];
		lpszDst[1] = acJisKatakanaTable[wCode * 2U + 1U];

		return 2;
#endif
	}

	inline const int PutSymbolsChar(TCHAR *lpszDst, const DWORD dwDstLen, const WORD wCode)
	{
		// 追加シンボル文字コード変換(とりあえず必要そうなものだけ)
		static const LPCTSTR aszSymbolsTable1[] =
		{
			_T("[HV]"),		_T("[SD]"),		_T("[Ｐ]"),		_T("[Ｗ]"),		_T("[MV]"),		_T("[手]"),		_T("[字]"),		_T("[双]"),			// 0x7A50 - 0x7A57	90/48 - 90/55
			_T("[デ]"),		_T("[Ｓ]"),		_T("[二]"),		_T("[多]"),		_T("[解]"),		_T("[SS]"),		_T("[Ｂ]"),		_T("[Ｎ]"),			// 0x7A58 - 0x7A5F	90/56 - 90/63
			_T("■"),		_T("●"),		_T("[天]"),		_T("[交]"),		_T("[映]"),		_T("[無]"),		_T("[料]"),		_T("[年齢制限]"),	// 0x7A60 - 0x7A67	90/64 - 90/71
			_T("[前]"),		_T("[後]"),		_T("[再]"),		_T("[新]"),		_T("[初]"),		_T("[終]"),		_T("[生]"),		_T("[販]"),			// 0x7A68 - 0x7A6F	90/72 - 90/79
			_T("[声]"),		_T("[吹]"),		_T("[PPV]"),	_T("(秘)"),		_T("ほか")															// 0x7A70 - 0x7A74	90/80 - 90/84
		};

#ifndef USE_UNICODE_CHAR
		static const LPCTSTR aszSymbolsTable2[] =
		{
			_T("→"),		_T("←"),		_T("↑"),		_T("↓"),		_T("○"),		_T("●"),		_T("年"),		_T("月"),			// 0x7C21 - 0x7C28	92/01 - 92/08
			_T("日"),		_T("円"),		_T("㎡"),		_T("立方ｍ"),	_T("㎝"),		_T("平方㎝"),	_T("立方㎝"),	_T("０."),			// 0x7C29 - 0x7C30	92/09 - 92/16
			_T("１."),		_T("２."),		_T("３."),		_T("４."),		_T("５."),		_T("６."),		_T("７."),		_T("８."),			// 0x7C31 - 0x7C38	92/17 - 92/24
			_T("９."),		_T("氏"),		_T("副"),		_T("元"),		_T("故"),		_T("前"),		_T("新"),		_T("０,"),			// 0x7C39 - 0x7C40	92/25 - 92/32
			_T("１,"),		_T("２,"),		_T("３,"),		_T("４,"),		_T("５,"),		_T("６,"),		_T("７,"),		_T("８,"),			// 0x7C41 - 0x7C48	92/33 - 92/40
			_T("９,"),		_T("(社)"),		_T("(財)"),		_T("(有)"),		_T("(株)"),		_T("(代)"),		_T("(問)"),		_T("＞"),			// 0x7C49 - 0x7C50	92/41 - 92/48
			_T("＜"),		_T("【"),		_T("】"),		_T("◇"),		_T("^2"),		_T("^3"),		_T("(CD)"),		_T("(vn)"),			// 0x7C51 - 0x7C58	92/49 - 92/56
			_T("(ob)"),		_T("(cb)"),		_T("(ce"),		_T("mb)"),		_T("(hp)"),		_T("(br)"),		_T("(p)"),		_T("(s)"),			// 0x7C59 - 0x7C60	92/57 - 92/64
			_T("(ms)"),		_T("(t)"),		_T("(bs)"),		_T("(b)"),		_T("(tb)"),		_T("(tp)"),		_T("(ds)"),		_T("(ag)"),			// 0x7C61 - 0x7C68	92/65 - 92/72
			_T("(eg)"),		_T("(vo)"),		_T("(fl)"),		_T("(ke"),		_T("y)"),		_T("(sa"),		_T("x)"),		_T("(sy"),			// 0x7C69 - 0x7C70	92/73 - 92/80
			_T("n)"),		_T("(or"),		_T("g)"),		_T("(pe"),		_T("r)"),		_T("(R)"),		_T("(C)"),		_T("(箏)"),			// 0x7C71 - 0x7C78	92/81 - 92/88
			_T("DJ"),		_T("[演]"),		_T("Fax")																							// 0x7C79 - 0x7C7B	92/89 - 92/91
		};
#else
		static const LPCWSTR aszSymbolsTable2[] =
		{
			_T("→"),		_T("←"),		_T("↑"),		_T("↓"),		_T("○"),		_T("●"),		_T("年"),		_T("月"),			// 0x7C21 - 0x7C28	92/01 - 92/08
			_T("日"),		_T("円"),		_T("㎡"),		_T("㎥"),		_T("㎝"),		_T("㎠"),		_T("㎤"),		_T("０."),			// 0x7C29 - 0x7C30	92/09 - 92/16
			_T("⒈"),		_T("⒉"),		_T("⒊"),		_T("⒋"),		_T("⒌"),		_T("⒍"),		_T("⒎"),		_T("⒏"),			// 0x7C31 - 0x7C38	92/17 - 92/24
			_T("⒐"),		_T("氏"),		_T("副"),		_T("元"),		_T("故"),		_T("前"),		_T("新"),		_T("０,"),			// 0x7C39 - 0x7C40	92/25 - 92/32
			_T("１,"),		_T("２,"),		_T("３,"),		_T("４,"),		_T("５,"),		_T("６,"),		_T("７,"),		_T("８,"),			// 0x7C41 - 0x7C48	92/33 - 92/40
			_T("９,"),		_T("(社)"),		_T("(財)"),		_T("(有)"),		_T("(株)"),		_T("(代)"),		_T("(問)"),		_T("▶"),			// 0x7C49 - 0x7C50	92/41 - 92/48
			_T("◀"),		_T("〖"),		_T("〗"),		_T("◇"),		_T("^2"),		_T("^3"),		_T("(CD)"),		_T("(vn)"),			// 0x7C51 - 0x7C58	92/49 - 92/56
			_T("(ob)"),		_T("(cb)"),		_T("(ce"),		_T("mb)"),		_T("(hp)"),		_T("(br)"),		_T("(p)"),		_T("(s)"),			// 0x7C59 - 0x7C60	92/57 - 92/64
			_T("(ms)"),		_T("(t)"),		_T("(bs)"),		_T("(b)"),		_T("(tb)"),		_T("(tp)"),		_T("(ds)"),		_T("(ag)"),			// 0x7C61 - 0x7C68	92/65 - 92/72
			_T("(eg)"),		_T("(vo)"),		_T("(fl)"),		_T("(ke"),		_T("y)"),		_T("(sa"),		_T("x)"),		_T("(sy"),			// 0x7C69 - 0x7C70	92/73 - 92/80
			_T("n)"),		_T("(or"),		_T("g)"),		_T("(pe"),		_T("r)"),		_T("Ⓡ"),		_T("Ⓒ"),		_T("(箏)"),			// 0x7C71 - 0x7C78	92/81 - 92/88
			_T("DJ"),		_T("[演]"),		_T("Fax")																							// 0x7C79 - 0x7C7B	92/89 - 92/91
		};
#endif

#ifndef USE_UNICODE_CHAR
		static const LPCTSTR aszSymbolsTable3[] =
		{
			_T("(月)"),		_T("(火)"),		_T("(水)"),		_T("(木)"),		_T("(金)"),		_T("(土)"),		_T("(日)"),		_T("(祝)"),			// 0x7D21 - 0x7D28	93/01 - 93/08
			_T("㍾"),		_T("㍽"),		_T("㍼"),		_T("㍻"),		_T("№"),		_T("℡"),		_T("(〒)"),		_T("○"),			// 0x7D29 - 0x7D30	93/09 - 93/16
			_T("〔本〕"),	_T("〔三〕"),	_T("〔二〕"),	_T("〔安〕"),	_T("〔点〕"),	_T("〔打〕"),	_T("〔盗〕"),	_T("〔勝〕"),		// 0x7D31 - 0x7D38	93/17 - 93/24
			_T("〔敗〕"),	_T("〔Ｓ〕"),	_T("［投］"),	_T("［捕］"),	_T("［一］"),	_T("［二］"),	_T("［三］"),	_T("［遊］"),		// 0x7D39 - 0x7D40	93/25 - 93/32
			_T("［左］"),	_T("［中］"),	_T("［右］"),	_T("［指］"),	_T("［走］"),	_T("［打］"),	_T("㍑"),		_T("㎏"),			// 0x7D41 - 0x7D48	93/33 - 93/40
			_T("Hz"),		_T("ha"),		_T("km"),		_T("平方km"),	_T("hPa"),		_T("・"),		_T("・"),		_T("1/2"),			// 0x7D49 - 0x7D50	93/41 - 93/48
			_T("0/3"),		_T("1/3"),		_T("2/3"),		_T("1/4"),		_T("3/4"),		_T("1/5"),		_T("2/5"),		_T("3/5"),			// 0x7D51 - 0x7D58	93/49 - 93/56
			_T("4/5"),		_T("1/6"),		_T("5/6"),		_T("1/7"),		_T("1/8"),		_T("1/9"),		_T("1/10"),		_T("晴れ"),			// 0x7D59 - 0x7D60	93/57 - 93/64
			_T("曇り"),		_T("雨"),		_T("雪"),		_T("△"),		_T("▲"),		_T("▽"),		_T("▼"),		_T("◆"),			// 0x7D61 - 0x7D68	93/65 - 93/72
			_T("・"),		_T("・"),		_T("・"),		_T("◇"),		_T("◎"),		_T("!!"),		_T("!?"),		_T("曇/晴"),		// 0x7D69 - 0x7D70	93/73 - 93/80
			_T("雨"),		_T("雨"),		_T("雪"),		_T("大雪"),		_T("雷"),		_T("雷雨"),		_T("　"),		_T("・"),			// 0x7D71 - 0x7D78	93/81 - 93/88
			_T("・"),		_T("♪"),		_T("℡")																							// 0x7D79 - 0x7D7B	93/89 - 93/91
		};
#else
		static const LPCWSTR aszSymbolsTable3[] =
		{
			_T("㈪"),		_T("㈫"),		_T("㈬"),		_T("㈭"),		_T("㈮"),		_T("㈯"),		_T("㈰"),		_T("㈷"),			// 0x7D21 - 0x7D28	93/01 - 93/08
			_T("㍾"),		_T("㍽"),		_T("㍼"),		_T("㍻"),		_T("№"),		_T("℡"),		_T("(〒)"),		_T("○"),			// 0x7D29 - 0x7D30	93/09 - 93/16
			_T("〔本〕"),	_T("〔三〕"),	_T("〔二〕"),	_T("〔安〕"),	_T("〔点〕"),	_T("〔打〕"),	_T("〔盗〕"),	_T("〔勝〕"),		// 0x7D31 - 0x7D38	93/17 - 93/24
			_T("〔敗〕"),	_T("〔Ｓ〕"),	_T("［投］"),	_T("［捕］"),	_T("［一］"),	_T("［二］"),	_T("［三］"),	_T("［遊］"),		// 0x7D39 - 0x7D40	93/25 - 93/32
			_T("［左］"),	_T("［中］"),	_T("［右］"),	_T("［指］"),	_T("［走］"),	_T("［打］"),	_T("㍑"),		_T("㎏"),			// 0x7D41 - 0x7D48	93/33 - 93/40
			_T("㎐"),		_T("㏊"),		_T("㎞"),		_T("㎢"),		_T("㍱"),		_T("・"),		_T("・"),		_T("1/2"),			// 0x7D49 - 0x7D50	93/41 - 93/48
			_T("0/3"),		_T("⅓"),		_T("⅔"),		_T("1/4"),		_T("3/4"),		_T("⅕"),		_T("⅖"),		_T("⅗"),			// 0x7D51 - 0x7D58	93/49 - 93/56
			_T("⅘"),		_T("⅙"),		_T("⅚"),		_T("1/7"),		_T("1/8"),		_T("1/9"),		_T("1/10"),		_T("☀"),			// 0x7D59 - 0x7D60	93/57 - 93/64
			_T("☁"),		_T("☂"),		_T("☃"),		_T("⌂"),		_T("▲"),		_T("▽"),		_T("▼"),		_T("♦"),			// 0x7D61 - 0x7D68	93/65 - 93/72
			_T("♥"),		_T("♣"),		_T("♠"),		_T("◇"),		_T("☉"),		_T("!!"),		_T("⁉"),		_T("曇/晴"),		// 0x7D69 - 0x7D70	93/73 - 93/80
			_T("雨"),		_T("雨"),		_T("雪"),		_T("大雪"),		_T("雷"),		_T("雷雨"),		_T("　"),		_T("・"),			// 0x7D71 - 0x7D78	93/81 - 93/88
			_T("・"),		_T("♬"),		_T("☎")																							// 0x7D79 - 0x7D7B	93/89 - 93/91
		};
#endif

#ifndef USE_UNICODE_CHAR
		static const LPCTSTR aszSymbolsTable4[] =
		{
			_T("Ⅰ"),		_T("Ⅱ"),		_T("Ⅲ"),		_T("Ⅳ"),		_T("Ⅴ"),		_T("Ⅵ"),		_T("Ⅶ"),		_T("Ⅷ"),			// 0x7E21 - 0x7E28	94/01 - 94/08
			_T("Ⅸ"),		_T("Ⅹ"),		_T("XI"),		_T("XⅡ"),		_T("⑰"),		_T("⑱"),		_T("⑲"),		_T("⑳"),			// 0x7E29 - 0x7E30	94/09 - 94/16
			_T("(1)"),		_T("(2)"),		_T("(3)"),		_T("(4)"),		_T("(5)"),		_T("(6)"),		_T("(7)"),		_T("(8)"),			// 0x7E31 - 0x7E38	94/17 - 94/24
			_T("(9)"),		_T("(10)"),		_T("(11)"),		_T("(12)"),		_T("(21)"),		_T("(22)"),		_T("(23)"),		_T("(24)"),			// 0x7E39 - 0x7E40	94/25 - 94/32
			_T("(A)"),		_T("(B)"),		_T("(C)"),		_T("(D)"),		_T("(E)"),		_T("(F)"),		_T("(G)"),		_T("(H)"),			// 0x7E41 - 0x7E48	94/33 - 94/40
			_T("(I)"),		_T("(J)"),		_T("(K)"),		_T("(L)"),		_T("(M)"),		_T("(N)"),		_T("(O)"),		_T("(P)"),			// 0x7E49 - 0x7E50	94/41 - 94/48
			_T("(Q)"),		_T("(R)"),		_T("(S)"),		_T("(T)"),		_T("(U)"),		_T("(V)"),		_T("(W)"),		_T("(X)"),			// 0x7E51 - 0x7E58	94/49 - 94/56
			_T("(Y)"),		_T("(Z)"),		_T("(25)"),		_T("(26)"),		_T("(27)"),		_T("(28)"),		_T("(29)"),		_T("(30)"),			// 0x7E59 - 0x7E60	94/57 - 94/64
			_T("①"),		_T("②"),		_T("③"),		_T("④"),		_T("⑤"),		_T("⑥"),		_T("⑦"),		_T("⑧"),			// 0x7E61 - 0x7E68	94/65 - 94/72
			_T("⑨"),		_T("⑩"),		_T("⑪"),		_T("⑫"),		_T("⑬"),		_T("⑭"),		_T("⑮"),		_T("⑯"),			// 0x7E69 - 0x7E70	94/73 - 94/80
			_T("①"),		_T("②"),		_T("③"),		_T("④"),		_T("⑤"),		_T("⑥"),		_T("⑦"),		_T("⑧"),			// 0x7E71 - 0x7E78	94/81 - 94/88
			_T("⑨"),		_T("⑩"),		_T("⑪"),		_T("⑫"),		_T("(31)")															// 0x7E79 - 0x7E7D	94/89 - 94/93
		};
#else
		static const LPCWSTR aszSymbolsTable4[] =
		{
			_T("Ⅰ"),		_T("Ⅱ"),		_T("Ⅲ"),		_T("Ⅳ"),		_T("Ⅴ"),		_T("Ⅵ"),		_T("Ⅶ"),		_T("Ⅷ"),			// 0x7E21 - 0x7E28	94/01 - 94/08
			_T("Ⅸ"),		_T("Ⅹ"),		_T("XI"),		_T("XⅡ"),		_T("⑰"),		_T("⑱"),		_T("⑲"),		_T("⑳"),			// 0x7E29 - 0x7E30	94/09 - 94/16
			_T("⑴"),		_T("⑵"),		_T("⑶"),		_T("⑷"),		_T("⑸"),		_T("⑹"),		_T("⑺"),		_T("⑻"),			// 0x7E31 - 0x7E38	94/17 - 94/24
			_T("⑼"),		_T("⑽"),		_T("⑾"),		_T("⑿"),		_T("㉑"),		_T("㉒"),		_T("㉓"),		_T("㉔"),			// 0x7E39 - 0x7E40	94/25 - 94/32
			_T("(A)"),		_T("(B)"),		_T("(C)"),		_T("(D)"),		_T("(E)"),		_T("(F)"),		_T("(G)"),		_T("(H)"),			// 0x7E41 - 0x7E48	94/33 - 94/40
			_T("(I)"),		_T("(J)"),		_T("(K)"),		_T("(L)"),		_T("(M)"),		_T("(N)"),		_T("(O)"),		_T("(P)"),			// 0x7E49 - 0x7E50	94/41 - 94/48
			_T("(Q)"),		_T("(R)"),		_T("(S)"),		_T("(T)"),		_T("(U)"),		_T("(V)"),		_T("(W)"),		_T("(X)"),			// 0x7E51 - 0x7E58	94/49 - 94/56
			_T("(Y)"),		_T("(Z)"),		_T("㉕"),		_T("㉖"),		_T("㉗"),		_T("㉘"),		_T("㉙"),		_T("㉚"),			// 0x7E59 - 0x7E60	94/57 - 94/64
			_T("①"),		_T("②"),		_T("③"),		_T("④"),		_T("⑤"),		_T("⑥"),		_T("⑦"),		_T("⑧"),			// 0x7E61 - 0x7E68	94/65 - 94/72
			_T("⑨"),		_T("⑩"),		_T("⑪"),		_T("⑫"),		_T("⑬"),		_T("⑭"),		_T("⑮"),		_T("⑯"),			// 0x7E69 - 0x7E70	94/73 - 94/80
			_T("❶"),		_T("❷"),		_T("❸"),		_T("❹"),		_T("❺"),		_T("❻"),		_T("❼"),		_T("❽"),			// 0x7E71 - 0x7E78	94/81 - 94/88
			_T("❾"),		_T("❿"),		_T("⓫"),		_T("⓬"),		_T("㉛")															// 0x7E79 - 0x7E7D	94/89 - 94/93
		};
#endif
		static const LPCWSTR aszKanjiTable1[] = {
			_T("㐂"),		_T("𠅘"),		_T("份"),		_T("仿"),		_T("侚"),		_T("俉"),		_T("傜"),		_T("儞"),			// 0x7521 - 0x7528
			_T("冼"),		_T("㔟"),		_T("匇"),		_T("卡"),		_T("卬"),		_T("詹"),		_T("𠮷"),		_T("呍"),			// 0x7529 - 0x7530
			_T("咖"),		_T("咜"),		_T("咩"),		_T("唎"),		_T("啊"),		_T("噲"),		_T("囤"),		_T("圳"),			// 0x7531 - 0x7538
			_T("圴"),		_T("塚"),		_T("墀"),		_T("姤"),		_T("娣"),		_T("婕"),		_T("寬"),		_T("﨑"),			// 0x7539 - 0x7540
			_T("㟢"),		_T("庬"),		_T("弴"),		_T("彅"),		_T("德"),		_T("怗"),		_T("・"),		_T("愰"),			// 0x7541 - 0x7548
			_T("昤"),		_T("曈"),		_T("曙"),		_T("曺"),		_T("曻"),		_T("・"),		_T("・"),		_T("椑"),			// 0x7549 - 0x7551
			_T("椻"),		_T("橅"),		_T("檑"),		_T("櫛"),		_T("𣏌"),		_T("𣏾"),		_T("𣗄"),		_T("毱"),			// 0x7550 - 0x7558
			_T("泠"),		_T("洮"),		_T("海"),		_T("涿"),		_T("淊"),		_T("淸"),		_T("渚"),		_T("潞"),			// 0x7559 - 0x7560
			_T("濹"),		_T("灤"),		_T("𤋮"),		_T("𤋮"),		_T("煇"),		_T("燁"),		_T("爀"),		_T("玟"),			// 0x7561 - 0x7568
			_T("玨"),		_T("珉"),		_T("珖"),		_T("琛"),		_T("琡"),		_T("琢"),		_T("琦"),		_T("琪"),			// 0x7569 - 0x7570
			_T("琬"),		_T("琹"),		_T("瑋"),		_T("㻚"),		_T("畵"),		_T("疁"),		_T("睲"),		_T("䂓"),			// 0x7571 - 0x7578
			_T("磈"),		_T("磠"),		_T("祇"),		_T("禮"),		_T("・"),		_T("・"),											// 0x7579 - 0x757E
		};
		static const LPCWSTR aszKanjiTable2[] = {
			_T("・"),		_T("秚"),		_T("稞"),		_T("筿"),		_T("簱"),		_T("䉤"),		_T("綋"),		_T("羡"),			// 0x7621 - 0x7628
			_T("脘"),		_T("脺"),		_T("舘"),		_T("芮"),		_T("葛"),		_T("蓜"),		_T("蓬"),		_T("蕙"),			// 0x7629 - 0x7630
			_T("藎"),		_T("蝕"),		_T("蟬"),		_T("蠋"),		_T("裵"),		_T("角"),		_T("諶"),		_T("跎"),			// 0x7631 - 0x7638
			_T("辻"),		_T("迶"),		_T("郝"),		_T("鄧"),		_T("鄭"),		_T("醲"),		_T("鈳"),		_T("銈"),			// 0x7639 - 0x7640
			_T("錡"),		_T("鍈"),		_T("閒"),		_T("雞"),		_T("餃"),		_T("饀"),		_T("髙"),		_T("鯖"),			// 0x7641 - 0x7648
			_T("鷗"),		_T("麴"),		_T("麵"),																							// 0x7649 - 0x764B
		};

		// シンボルを変換する
		LPCTSTR pszSrc;
		if ((wCode >= 0x7521) && (wCode <= 0x757E)) {
			pszSrc = aszKanjiTable1[wCode - 0x7521];
		}
		else if ((wCode >= 0x7621) && (wCode <= 0x764B)) {
			pszSrc = aszKanjiTable2[wCode - 0x7621];
		}
		else if ((wCode >= 0x7A50U) && (wCode <= 0x7A74U)) {
			pszSrc = aszSymbolsTable1[wCode - 0x7A50U];
		}
		else if ((wCode >= 0x7C21U) && (wCode <= 0x7C7BU)) {
			pszSrc = aszSymbolsTable2[wCode - 0x7C21U];
		}
		else if ((wCode >= 0x7D21U) && (wCode <= 0x7D7BU)) {
			pszSrc = aszSymbolsTable3[wCode - 0x7D21U];
		}
		else if ((wCode >= 0x7E21U) && (wCode <= 0x7E7DU)) {
			pszSrc = aszSymbolsTable4[wCode - 0x7E21U];
		}
		else {
			pszSrc = _T("□");
		}
		DWORD Length = lstrlen(pszSrc);
		if (dwDstLen < Length)
			return -1;
		::CopyMemory(lpszDst, pszSrc, Length * sizeof(TCHAR));

		return Length;
	}

	inline const int PutMacroChar(TCHAR *lpszDst, const DWORD dwDstLen, const WORD wCode)
	{
		static const BYTE Macro[16][20] = {
			{ 0x1B, 0x24, 0x39, 0x1B, 0x29, 0x4A, 0x1B, 0x2A, 0x30, 0x1B, 0x2B, 0x20, 0x70, 0x0F, 0x1B, 0x7D },
			{ 0x1B, 0x24, 0x39, 0x1B, 0x29, 0x31, 0x1B, 0x2A, 0x30, 0x1B, 0x2B, 0x20, 0x70, 0x0F, 0x1B, 0x7D },
			{ 0x1B, 0x24, 0x39, 0x1B, 0x29, 0x20, 0x41, 0x1B, 0x2A, 0x30, 0x1B, 0x2B, 0x20, 0x70, 0x0F, 0x1B, 0x7D },
			{ 0x1B, 0x28, 0x32, 0x1B, 0x29, 0x34, 0x1B, 0x2A, 0x35, 0x1B, 0x2B, 0x20, 0x70, 0x0F, 0x1B, 0x7D },
			{ 0x1B, 0x28, 0x32, 0x1B, 0x29, 0x33, 0x1B, 0x2A, 0x35, 0x1B, 0x2B, 0x20, 0x70, 0x0F, 0x1B, 0x7D },
			{ 0x1B, 0x28, 0x32, 0x1B, 0x29, 0x20, 0x41, 0x1B, 0x2A, 0x35, 0x1B, 0x2B, 0x20, 0x70, 0x0F, 0x1B, 0x7D },
			{ 0x1B, 0x28, 0x20, 0x41, 0x1B, 0x29, 0x20, 0x42, 0x1B, 0x2A, 0x20, 0x43, 0x1B, 0x2B, 0x20, 0x70, 0x0F, 0x1B, 0x7D },
			{ 0x1B, 0x28, 0x20, 0x44, 0x1B, 0x29, 0x20, 0x45, 0x1B, 0x2A, 0x20, 0x46, 0x1B, 0x2B, 0x20, 0x70, 0x0F, 0x1B, 0x7D },
			{ 0x1B, 0x28, 0x20, 0x47, 0x1B, 0x29, 0x20, 0x48, 0x1B, 0x2A, 0x20, 0x49, 0x1B, 0x2B, 0x20, 0x70, 0x0F, 0x1B, 0x7D },
			{ 0x1B, 0x28, 0x20, 0x4A, 0x1B, 0x29, 0x20, 0x4B, 0x1B, 0x2A, 0x20, 0x4C, 0x1B, 0x2B, 0x20, 0x70, 0x0F, 0x1B, 0x7D },
			{ 0x1B, 0x28, 0x20, 0x4D, 0x1B, 0x29, 0x20, 0x4E, 0x1B, 0x2A, 0x20, 0x4F, 0x1B, 0x2B, 0x20, 0x70, 0x0F, 0x1B, 0x7D },
			{ 0x1B, 0x24, 0x39, 0x1B, 0x29, 0x20, 0x42, 0x1B, 0x2A, 0x30, 0x1B, 0x2B, 0x20, 0x70, 0x0F, 0x1B, 0x7D },
			{ 0x1B, 0x24, 0x39, 0x1B, 0x29, 0x20, 0x43, 0x1B, 0x2A, 0x30, 0x1B, 0x2B, 0x20, 0x70, 0x0F, 0x1B, 0x7D },
			{ 0x1B, 0x24, 0x39, 0x1B, 0x29, 0x20, 0x44, 0x1B, 0x2A, 0x30, 0x1B, 0x2B, 0x20, 0x70, 0x0F, 0x1B, 0x7D },
			{ 0x1B, 0x28, 0x31, 0x1B, 0x29, 0x30, 0x1B, 0x2A, 0x4A, 0x1B, 0x2B, 0x20, 0x70, 0x0F, 0x1B, 0x7D },
			{ 0x1B, 0x28, 0x4A, 0x1B, 0x29, 0x32, 0x1B, 0x2A, 0x20, 0x41, 0x1B, 0x2B, 0x20, 0x70, 0x0F, 0x1B, 0x7D },
		};

		if ((wCode & 0xF0) == 0x60)
			return ProcessString(lpszDst, dwDstLen, Macro[wCode & 0x0F], ::lstrlenA((LPCSTR)Macro[wCode & 0x0F]));
		return 0;
	}

	inline const int PutDRCSChar(TCHAR *lpszDst, const DWORD dwDstLen, const WORD wCode)
	{
		if (m_pDRCSMap) {
			LPCTSTR pszSrc = m_pDRCSMap->GetString(wCode);
			if (pszSrc) {
				int Length = lstrlen(pszSrc);
				if (dwDstLen < (DWORD)Length)
					return -1;
				::CopyMemory(lpszDst, pszSrc, Length * sizeof(TCHAR));
				return Length;
			}
		}
#ifdef _UNICODE
		if (dwDstLen < 1)
			return -1;
		lpszDst[0] = L'□';
		return 1;
#else
		if (dwDstLen < 2)
			return -1;
		lpszDst[0] = "□"[0];
		lpszDst[1] = "□"[1];
		return 2;
#endif
	}


	inline void ProcessEscapeSeq(const BYTE byCode)
	{
		// エスケープシーケンス処理
		switch (m_byEscSeqCount) {
			// 1バイト目
		case 1U:
			switch (byCode) {
				// Invocation of code elements
			case 0x6EU: LockingShiftGL(2U);	m_byEscSeqCount = 0U;	return;		// LS2
			case 0x6FU: LockingShiftGL(3U);	m_byEscSeqCount = 0U;	return;		// LS3
			case 0x7EU: LockingShiftGR(1U);	m_byEscSeqCount = 0U;	return;		// LS1R
			case 0x7DU: LockingShiftGR(2U);	m_byEscSeqCount = 0U;	return;		// LS2R
			case 0x7CU: LockingShiftGR(3U);	m_byEscSeqCount = 0U;	return;		// LS3R

																																			// Designation of graphic sets
			case 0x24U:
			case 0x28U: m_byEscSeqIndex = 0U;		break;
			case 0x29U: m_byEscSeqIndex = 1U;		break;
			case 0x2AU: m_byEscSeqIndex = 2U;		break;
			case 0x2BU: m_byEscSeqIndex = 3U;		break;
			default: m_byEscSeqCount = 0U;		return;		// エラー
			}
			break;

			// 2バイト目
		case 2U:
			if (DesignationGSET(m_byEscSeqIndex, byCode)) {
				m_byEscSeqCount = 0U;
				return;
			}

			switch (byCode) {
			case 0x20: m_bIsEscSeqDrcs = true;	break;
			case 0x28: m_bIsEscSeqDrcs = true;	m_byEscSeqIndex = 0U;	break;
			case 0x29: m_bIsEscSeqDrcs = false;	m_byEscSeqIndex = 1U;	break;
			case 0x2A: m_bIsEscSeqDrcs = false;	m_byEscSeqIndex = 2U;	break;
			case 0x2B: m_bIsEscSeqDrcs = false;	m_byEscSeqIndex = 3U;	break;
			default: m_byEscSeqCount = 0U;		return;		// エラー
			}
			break;

			// 3バイト目
		case 3U:
			if (!m_bIsEscSeqDrcs) {
				if (DesignationGSET(m_byEscSeqIndex, byCode)) {
					m_byEscSeqCount = 0U;
					return;
				}
			}
			else {
				if (DesignationDRCS(m_byEscSeqIndex, byCode)) {
					m_byEscSeqCount = 0U;
					return;
				}
			}

			if (byCode == 0x20U) {
				m_bIsEscSeqDrcs = true;
			}
			else {
				// エラー
				m_byEscSeqCount = 0U;
				return;
			}
			break;

			// 4バイト目
		case 4U:
			DesignationDRCS(m_byEscSeqIndex, byCode);
			m_byEscSeqCount = 0U;
			return;
		}

		m_byEscSeqCount++;
	}


	inline void LockingShiftGL(const BYTE byIndexG)
	{
		// LSx
		m_pLockingGL = &m_CodeG[byIndexG];
	}

	inline void LockingShiftGR(const BYTE byIndexG)
	{
		// LSxR
		m_pLockingGR = &m_CodeG[byIndexG];
	}

	inline void SingleShiftGL(const BYTE byIndexG)
	{
		// SSx
		m_pSingleGL = &m_CodeG[byIndexG];
	}

	inline const bool DesignationGSET(const BYTE byIndexG, const BYTE byCode)
	{
		// Gのグラフィックセットを割り当てる
		switch (byCode) {
		case 0x42U: m_CodeG[byIndexG] = CODE_KANJI;				return true;	// Kanji
		case 0x4AU: m_CodeG[byIndexG] = CODE_ALPHANUMERIC;		return true;	// Alphanumeric
		case 0x30U: m_CodeG[byIndexG] = CODE_HIRAGANA;			return true;	// Hiragana
		case 0x31U: m_CodeG[byIndexG] = CODE_KATAKANA;			return true;	// Katakana
		case 0x32U: m_CodeG[byIndexG] = CODE_MOSAIC_A;			return true;	// Mosaic A
		case 0x33U: m_CodeG[byIndexG] = CODE_MOSAIC_B;			return true;	// Mosaic B
		case 0x34U: m_CodeG[byIndexG] = CODE_MOSAIC_C;			return true;	// Mosaic C
		case 0x35U: m_CodeG[byIndexG] = CODE_MOSAIC_D;			return true;	// Mosaic D
		case 0x36U: m_CodeG[byIndexG] = CODE_PROP_ALPHANUMERIC;	return true;	// Proportional Alphanumeric
		case 0x37U: m_CodeG[byIndexG] = CODE_PROP_HIRAGANA;		return true;	// Proportional Hiragana
		case 0x38U: m_CodeG[byIndexG] = CODE_PROP_KATAKANA;		return true;	// Proportional Katakana
		case 0x49U: m_CodeG[byIndexG] = CODE_JIS_X0201_KATAKANA;	return true;	// JIS X 0201 Katakana
		case 0x39U: m_CodeG[byIndexG] = CODE_JIS_KANJI_PLANE_1;	return true;	// JIS compatible Kanji Plane 1
		case 0x3AU: m_CodeG[byIndexG] = CODE_JIS_KANJI_PLANE_2;	return true;	// JIS compatible Kanji Plane 2
		case 0x3BU: m_CodeG[byIndexG] = CODE_ADDITIONAL_SYMBOLS;	return true;	// Additional symbols
		default: return false;		// 不明なグラフィックセット
		}
	}

	inline const bool DesignationDRCS(const BYTE byIndexG, const BYTE byCode)
	{
		// DRCSのグラフィックセットを割り当てる
		if (byCode >= 0x40 && byCode <= 0x4F) {		// DRCS
			m_CodeG[byIndexG] = (CODE_SET)(CODE_DRCS_0 + (byCode - 0x40));
		}
		else if (byCode == 0x70) {				// Macro
			m_CodeG[byIndexG] = CODE_MACRO;
		}
		else {
			return false;
		}
		return true;
	}

	inline const bool IsSmallCharMode() const {
		return m_CharSize == SIZE_SMALL || m_CharSize == SIZE_MEDIUM || m_CharSize == SIZE_MICRO;
	}

	bool SetFormat(DWORD Pos)
	{
		if (!m_pFormatList)
			return false;

		FormatInfo Format;
		Format.Pos = Pos;
		Format.Size = m_CharSize;
		Format.CharColorIndex = m_CharColorIndex;
		Format.BackColorIndex = m_BackColorIndex;
		Format.RasterColorIndex = m_RasterColorIndex;

		if (!m_pFormatList->empty()) {
			if (m_pFormatList->back().Pos == Pos) {
				m_pFormatList->back() = Format;
				return true;
			}
		}
		m_pFormatList->push_back(Format);
		return true;
	}
};

#undef _T

#ifdef __HACK_UNICODE__
#undef __HACK_UNICODE__
#undef _UNICODE
#endif

} // namespace aribstring

#include <memory>

std::wstring GetAribString(MemoryChunk mc) {
	int bufsize = (int)mc.length + 1;
	auto buf = std::unique_ptr<wchar_t[]>(new wchar_t[bufsize]);
	int dstLen = aribstring::CAribString::AribToString(buf.get(), bufsize, mc.data, (int)mc.length);
	return std::wstring(buf.get(), buf.get() + dstLen);
}
