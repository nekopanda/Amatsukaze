// IIR_3DNRフィルタ  by H_Kasahara(aka.HK) より拝借
// シーンチェンジ検出用にアルゴリズム改修 by Yobi


//---------------------------------------------------------------------
//		動き検索処理用
//---------------------------------------------------------------------

#include "stdafx.h"
#include <Windows.h>
#include <stdlib.h>
#include <limits.h>
#include <stdio.h>

#define FRAME_PICTURE	1
#define FIELD_PICTURE	2
#define MAX_SEARCH_EXTENT 32	//全探索の最大探索範囲。+-この値まで。
#define RATE_SCENE_CHANGE 8		//シーンチェンジと判定する割合％
#define THRES_STILLDATA   8     //ベタ塗り画像用に誤差範囲と判断させる適当な値
#define THRES_CMPPIX      40	//補足処理実行用閾値

//---------------------------------------------------------------------
//		関数定義
//---------------------------------------------------------------------
//void make_motion_lookup_table();
//BOOL mvec(unsigned char* current_pix,unsigned char* bef_pix,int* vx,int* vy,int lx,int ly,int threshold,int pict_struct,int SC_level);
int mvec(int *mvec2,int *flag_sc,unsigned char* current_pix,unsigned char* bef_pix,int lx,int ly,int threshold,int pict_struct);
int tree_search(unsigned char* current_pix,unsigned char* bef_pix,int lx,int ly,int *vx,int *vy,int search_block_x,int search_block_y,int min,int pict_struct, int method);
int full_search(unsigned char* current_pix,unsigned char* bef_pix,int lx,int ly,int *vx,int *vy,int search_block_x,int search_block_y,int min,int pict_struct, int search_extent);
int dist( unsigned char *p1, unsigned char *p2, int lx, int distlim, int block_hight );
int maxmin_block( unsigned char *p, int lx, int block_height );

//---------------------------------------------------------------------
//		グローバル変数
//---------------------------------------------------------------------
int	block_hight, lx2;


//---------------------------------------------------------------------
//		動き誤差判定関数
//---------------------------------------------------------------------
//[ru] 動きベクトルの合計を返す
int tree=0, full=0;
int mvec( int *mvec2,					//インターレースで動きが少ない側の結果を格納（出力）
          int *flag_sc,					//シーンチェンジフラグ（出力）
		  unsigned char* current_pix, 	//現フレームの輝度。8ビット。
		  unsigned char* bef_pix,		//前フレームの輝度。8ビット。
		  int lx,						//画像の横幅
		  int ly,						//画像の縦幅
		  int threshold,				//検索精度。(100-fp->track[1])*50 …… 50は適当な値。
		  int pict_struct)				//"1"ならフレーム処理、"2"ならフィールド処理
{
	int x, y;
	unsigned char *p1, *p2;
	int motion_vector_total = 0;
	int calc_total_lane0, calc_total_lane1;
	int calc_total;
	int cnt_sc, cnt_scb, cnt_total;
	int rate_sc, b_sc, b_sc0, b_sc1;

//関数を呼び出す毎に計算せずにすむようグローバル変数とする
	lx2 = lx*pict_struct;
	block_hight = 16/pict_struct;

	for(int i=0;i<pict_struct;i++)
	{
		calc_total = 0;
		cnt_sc = 0;
		cnt_scb = 0;
		cnt_total = 0;
		for(y=i;y<ly;y+=16)	//全体縦軸
		{
			p1 = current_pix + y*lx;
			p2 = bef_pix + y*lx;
			for(x=0;x<lx;x+=16)	//全体横軸
			{
				int vx=0, vy=0;
				int method = 0;
				int min = dist( p1, p2, lx2, INT_MAX, block_hight );
				int minnew = min;		//停止時の動き情報を記憶（補足処理用）
				int vxnew = 0;			//（補足処理用）
				int vynew = 0;			//（補足処理用）
				int b_nodif = 0;		//（補足処理用）
				if (threshold >= min){	//フレーム間の絶対値差が最初から小さければ簡略化
					//method = 1;		//動き情報も考慮に入れるならこちら
					method = 2;			//速度優先ならこちら
					b_nodif = 1;		//動きなしを記憶（補足処理用）
				}
				if( threshold < (min = tree_search( p1, p2, lx, ly, &vx, &vy, x, y, min, pict_struct, method)))
//フレーム間の絶対値差が大きければ全探索をおこなう
					if ( threshold < (min = full_search( p1, &p2[vy * lx + vx], lx, ly, &vx, &vy, x+vx, y+vy, min, pict_struct, max(abs(vx),abs(vy))*2 ))){
						vx = MAX_SEARCH_EXTENT * 10;		// 検出できなかった場合、大きな値を設定
						vy = MAX_SEARCH_EXTENT * 10;
						cnt_sc ++;
					}
//動きベクトルの合計がシーンチェンジレベルを超えていたら、シーンチェンジと判定して大きな値を設定

				if (threshold >= min && b_nodif == 0){
//（補足処理）動きありと判定された場合、現フレームだけが空白に近い場合は前フレームの動きを検出
//前フレーム表示物が消えた場合は通常方法では検出できないケースがあり補足として追加
					if ( maxmin_block(p1, lx2, block_hight) * 2 + THRES_CMPPIX <= maxmin_block(p2, lx2, block_hight) ){
						if( threshold < (minnew = tree_search( p2, p1, lx, ly, &vxnew, &vynew, x, y, minnew, pict_struct, method)))
							if ( threshold < (minnew = full_search( p2, &p1[vynew * lx + vxnew], lx, ly, &vxnew, &vynew, x+vxnew, y+vynew, minnew, pict_struct, max(abs(vxnew),abs(vynew))*2 ))){
								vx = MAX_SEARCH_EXTENT * 4;		// 検出できなかった場合、補足前よりは大きな値を設定
								vy = MAX_SEARCH_EXTENT * 4;
								cnt_scb ++;
							}
					}
				}

				calc_total += abs(vx)+abs(vy);

				p1+=16;
				p2+=16;
				cnt_total ++;
			}
		}
		// シーンチェンジの割合を計算（補足処理分は影響少なめに補正）
		rate_sc = (cnt_sc + cnt_scb/2) * 100 / cnt_total;
		if (rate_sc >= RATE_SCENE_CHANGE){
			b_sc = 1;
		}
		else{
			b_sc = 0;
		}
		// インターレースはトップ／ボトムで別々に記憶。未計算と区別で最低１以上にするため１を加算
		if (i == 0){
			calc_total_lane0 = calc_total+1;
			b_sc0 = b_sc;
		}
		else{
			calc_total_lane1 = calc_total+1;
			b_sc1 = b_sc;
		}
	}

	if (pict_struct != 2){				// フレーム処理の場合、結果をそのまま代入
		motion_vector_total = calc_total_lane0;
		*mvec2 = calc_total_lane0;
		*flag_sc = b_sc0;
	}
	else if (calc_total_lane0 >= calc_total_lane1){		// インターレースでlane0の方が動き大きい時
		motion_vector_total = calc_total_lane0;
		*mvec2 = calc_total_lane1;
		*flag_sc = b_sc0;
	}
	else{												// インターレースでlane1の方が動き大きい時
		motion_vector_total = calc_total_lane1;
		*mvec2 = calc_total_lane0;
		*flag_sc = b_sc1;
	}

	/*char str[500];
	sprintf_s(str, 500, "tree:%d, full:%d", tree, full);
	MessageBox(NULL, str, 0, 0);*/

	return motion_vector_total;
}
//---------------------------------------------------------------------
//		簡易探索法動き検索関数
//      同じ値の場合は中心に近い方を選択する
//---------------------------------------------------------------------
int tree_search(unsigned char* current_pix,	//現フレームの輝度。8ビット。
				unsigned char* bef_pix,		//前フレームの輝度。8ビット。
				int lx,						//画像の横幅
				int ly,						//画像の縦幅
				int *vx,					//x方向の動きベクトルが代入される。
				int *vy,					//y方向の動きベクトルが代入される。
				int search_block_x,			//検索位置
				int search_block_y,			//検索位置
				int min,					//同位置でのフレーム間の絶対値差。関数内では同位置の比較をしないので、呼び出す前に行う必要あり。
				int pict_struct,			//"1"ならフレーム処理、"2"ならフィールド処理
				int method)					//検索の簡易化（0:探索多回数 1:２分探索 2:検索省略）
{
	tree++;
	int dx, dy, ddx=0, ddy=0, xs=0, ys;
	int d;
	int x,y;
	int locx, locy;
	int loopmax, inter;
	int nrep, step, dthres;
	int speedup = pict_struct-1;
//検索範囲の上限と下限を設定
	int ylow  = 0 - search_block_y;
	int yhigh = ly- search_block_y-16;
	int xlow  = 0 - search_block_x;
	int xhigh = lx- search_block_x-16;

	if (method == 2) return min;	// 検索省略

	if (method == 0){
		loopmax = 3-speedup;
		inter = 0;					// interは不使用
	}
	else{
		loopmax = 5-speedup;		// MAX_SEARCH_EXTENT=32の時（計算省略のため直接定義）
		inter = MAX_SEARCH_EXTENT;
	}
	for(int i=0; i<loopmax; i++){
		if (method == 0){			// ２段階で検索（フィールド処理で比較合計９６回）
			if (i==0){
				locx = MAX_SEARCH_EXTENT - 8;
				locy = MAX_SEARCH_EXTENT - 8;
				nrep = MAX_SEARCH_EXTENT/8*2 - 1;
				step = 8;
				dthres = THRES_STILLDATA << 4;		// 誤差範囲とする適当な値
			}
			else if (i==1){
				locx = ddx - 6;
				locy = ddy - 6;
				nrep = 7;
				step = 2;
				dthres = THRES_STILLDATA << 2;		// 誤差範囲とする適当な値
			}
			else{
				locx = ddx - 1;
				locy = ddy - 1;
				nrep = 3;
				step = 1;
				dthres = 1;			// 誤差範囲とする適当な値
			}
		}
		else{						// ２分探索（フィールド処理で比較合計３２回）
			inter = inter / 2;
			locx = ddx - inter;
			locy = ddy - inter;
			nrep = 3;
			step = inter;
			dthres = THRES_STILLDATA << (loopmax - i - 1);		// 誤差範囲とする適当な値
		}
		// 検索開始
		dy = locy;
		for(y=0; y<nrep; y++){
			if ( dy<ylow || dy>yhigh ){			//検索位置が画面外に出ていたら検索をおこなわない。
			}
			else{
				ys = dy * lx;	//検索位置縦軸
				dx = locx;
				for(x=0; x<nrep; x++){
					if( dx<xlow || dx>xhigh ){	//検索位置が画面外に出ていたら検索をおこなわない。
					}
					else if (x == (nrep-1)/2 && y == (nrep-1)/2){	// 中心座標では計算しない。
					}
					else{
						d = dist( current_pix, &bef_pix[ys+dx], lx2, min, block_hight );
						if( d <= min ){	//これまでの検索よりフレーム間の絶対値差が小さかったらそれぞれ代入。
							if ((d + dthres <= min) ||
								(abs(dx) + abs(dy) <= abs(ddx) - abs(ddy))){	// 中心に近いか、誤差閾値以上差がある場合セット
									min = d;
									ddx = dx;
									ddy = dy;
							}
						}
					}
					dx += step;
				}
			}
			dy += step;
		}
	}

	if(pict_struct==FIELD_PICTURE){
		for(x=0,dx=ddx-1;x<3;x+=2,dx+=2){
			if( search_block_x+dx<0 || search_block_x+dx+16>lx )	continue;	//検索位置が画面外に出ていたら検索をおこなわない。
			d = dist( current_pix, &bef_pix[ys+dx], lx2, min, block_hight );
			if( d < min ){	//これまでの検索よりフレーム間の絶対値差が小さかったらそれぞれ代入。
				min = d;
				ddx = dx;
			}
		}
	}
	

	*vx += ddx;
	*vy += ddy;

	return min;
}
//---------------------------------------------------------------------
//		全探索法動き検索関数
//      同じ値の場合は中心に近い方を選択する
//---------------------------------------------------------------------
int full_search(unsigned char* current_pix,	//現フレームの輝度。8ビット。
				unsigned char* bef_pix,		//前フレームの輝度。8ビット。
				int lx,						//画像の横幅
				int ly,						//画像の縦幅
				int *vx,					//x方向の動きベクトルが代入される。
				int *vy,					//y方向の動きベクトルが代入される。
				int search_block_x,			//検索位置
				int search_block_y,			//検索位置
				int min,					//フレーム間の絶対値差。最初の探索ではINT_MAXが入っている。
				int pict_struct,			//"1"ならフレーム処理、"2"ならフィールド処理
				int search_extent)			//探索範囲。
{
	full++;
	int dx, dy, ddx=0, ddy=0;
	int d;
	int dthres;
//	int search_point;
	unsigned char* p2;

	if(search_extent>MAX_SEARCH_EXTENT)
		search_extent = MAX_SEARCH_EXTENT;

//検索範囲の上限と下限が画像からはみ出していないかチェック
	int ylow  = 0 - ( (search_block_y-search_extent<0) ? search_block_y : search_extent );
	int yhigh = (search_block_y+search_extent+16>ly) ? ly-search_block_y-16 : search_extent;
	int xlow  = 0 - ( (search_block_x-search_extent<0) ? search_block_x : search_extent );
	int xhigh = (search_block_x+search_extent+16>lx) ? lx-search_block_x-16 : search_extent;

	dthres = THRES_STILLDATA;		// 誤差範囲とする適当な値
	for(dy=ylow;dy<=yhigh;dy+=pict_struct)
	{
		p2 = bef_pix + dy*lx + xlow;	//Y軸検索位置。xlowは負の値なので"p2=bef_pix+dy*lx-xlow"とはならない
		for(dx=xlow;dx<=xhigh;dx++)
		{
			d = dist( current_pix, p2, lx2, min, block_hight );
			if(d <= min)	//これまでの検索よりフレーム間の絶対値差が小さかったらそれぞれ代入。
			{
				if ((d + dthres <= min) ||
					(abs(dx) + abs(dy) <= abs(ddx) - abs(ddy))){	// 中心に近いか、誤差閾値以上差がある場合セット
					min = d;
					ddx = dx;
					ddy = dy;
				}
			}
			p2++;
		}
	}

	*vx += ddx;
	*vy += ddy;

	return min;
}
//---------------------------------------------------------------------
//		フレーム間絶対値差合計関数
//---------------------------------------------------------------------
//bbMPEGのソースを流用
#include <emmintrin.h>

int dist( unsigned char *p1, unsigned char *p2, int lx, int distlim, int block_height )
{
	if (block_height == 8) {
		__m128i a, b, r;

		a = _mm_load_si128 ((__m128i*)p1 +  0);
		b = _mm_loadu_si128((__m128i*)p2 +  0);
		r = _mm_sad_epu8(a, b);

		a = _mm_load_si128 ((__m128i*)(p1 + lx));
		b = _mm_loadu_si128((__m128i*)(p2 + lx));
		r = _mm_add_epi32(r, _mm_sad_epu8(a, b));

		a = _mm_load_si128 ((__m128i*)(p1 + 2*lx));
		b = _mm_loadu_si128((__m128i*)(p2 + 2*lx));
		r = _mm_add_epi32(r, _mm_sad_epu8(a, b));

		a = _mm_load_si128 ((__m128i*)(p1 + 3*lx));
		b = _mm_loadu_si128((__m128i*)(p2 + 3*lx));
		r = _mm_add_epi32(r, _mm_sad_epu8(a, b));

		a = _mm_load_si128 ((__m128i*)(p1 + 4*lx));
		b = _mm_loadu_si128((__m128i*)(p2 + 4*lx));
		r = _mm_add_epi32(r, _mm_sad_epu8(a, b));

		a = _mm_load_si128 ((__m128i*)(p1 + 5*lx));
		b = _mm_loadu_si128((__m128i*)(p2 + 5*lx));
		r = _mm_add_epi32(r, _mm_sad_epu8(a, b));

		a = _mm_load_si128 ((__m128i*)(p1 + 6*lx));
		b = _mm_loadu_si128((__m128i*)(p2 + 6*lx));
		r = _mm_add_epi32(r, _mm_sad_epu8(a, b));

		a = _mm_load_si128 ((__m128i*)(p1 + 7*lx));
		b = _mm_loadu_si128((__m128i*)(p2 + 7*lx));
		r = _mm_add_epi32(r, _mm_sad_epu8(a, b));
		return _mm_extract_epi16(r, 0) + _mm_extract_epi16(r, 4);;
	}

	int s = 0;
	for(int i=0;i<block_height;i++)
	{
		/*
		s += motion_lookup[p1[0]][p2[0]];
		s += motion_lookup[p1[1]][p2[1]];
		s += motion_lookup[p1[2]][p2[2]];
		s += motion_lookup[p1[3]][p2[3]];
		s += motion_lookup[p1[4]][p2[4]];
		s += motion_lookup[p1[5]][p2[5]];
		s += motion_lookup[p1[6]][p2[6]];
		s += motion_lookup[p1[7]][p2[7]];
		s += motion_lookup[p1[8]][p2[8]];
		s += motion_lookup[p1[9]][p2[9]];
		s += motion_lookup[p1[10]][p2[10]];
		s += motion_lookup[p1[11]][p2[11]];
		s += motion_lookup[p1[12]][p2[12]];
		s += motion_lookup[p1[13]][p2[13]];
		s += motion_lookup[p1[14]][p2[14]];
		s += motion_lookup[p1[15]][p2[15]];*/

		__m128i a = _mm_load_si128((__m128i*)p1);
		__m128i b = _mm_loadu_si128((__m128i*)p2);
		__m128i r = _mm_sad_epu8(a, b);
		s += _mm_extract_epi16(r, 0) + _mm_extract_epi16(r, 4);

		if (s > distlim)	break;

		p1 += lx;
		p2 += lx;
	}
	return s;
}


//---------------------------------------------------------------------
//		フレーム間絶対値差合計関数(SSEバージョン)
//---------------------------------------------------------------------
int dist_SSE( unsigned char *p1, unsigned char *p2, int lx, int distlim, int block_hight )
{
	int s = 0;
/*
dist_normalを見ると分かるように、p1とp2の絶対値差を足してき、distlimを超えたらその合計を返すだけ。
block_hightには8か16が代入されており、前者はフィールド処理、後者がフレーム処理用。
block_hightに8が代入されていたらば、lxには画像の横幅が代入されている。
block_hightに16が代入されていたらば、lxには画像の横幅の二倍の値が代入されている。
どなたか、ここを作成していただけたらば、非常に感謝いたします。
*/
	return s;
}


//---------------------------------------------------------------------
//		ブロック内の最大輝度差取得関数
//---------------------------------------------------------------------
int maxmin_block( unsigned char *p, int lx, int block_height )
{
	__m128i rmin, rmax, a, b, z;

	// 各列の最大・最小を求める
	rmin = _mm_load_si128((__m128i*)p);
	rmax = _mm_load_si128((__m128i*)p);
	p += lx;
	for(int i=1; i<block_height; i++){
		a = _mm_load_si128((__m128i*)p);
		rmin = _mm_min_epu8(rmin, a);
		rmax = _mm_max_epu8(rmax, a);
		p += lx;
	}
	// 列間の最大・最小を求める
	// 16データの最大・最小を８データに絞る
	z    = _mm_setzero_si128();
	a    = _mm_unpackhi_epi8(rmin, z);
	b    = _mm_unpacklo_epi8(rmin, z);
	rmin = _mm_min_epi16(a, b);
	a    = _mm_unpackhi_epi8(rmax, z);
	b    = _mm_unpacklo_epi8(rmax, z);
	rmax = _mm_max_epi16(a, b);
	// 8から4
	a    = _mm_unpackhi_epi16(rmin, z);
	b    = _mm_unpacklo_epi16(rmin, z);
	rmin = _mm_min_epi16(a, b);
	a    = _mm_unpackhi_epi16(rmax, z);
	b    = _mm_unpacklo_epi16(rmax, z);
	rmax = _mm_max_epi16(a, b);
	// 4から2
	a    = _mm_unpackhi_epi32(rmin, z);
	b    = _mm_unpacklo_epi32(rmin, z);
	rmin = _mm_min_epi16(a, b);
	a    = _mm_unpackhi_epi32(rmax, z);
	b    = _mm_unpacklo_epi32(rmax, z);
	rmax = _mm_max_epi16(a, b);
	// 2から1
	a    = _mm_unpackhi_epi64(rmin, z);
	b    = _mm_unpacklo_epi64(rmin, z);
	rmin = _mm_min_epi16(a, b);
	a    = _mm_unpackhi_epi64(rmax, z);
	b    = _mm_unpacklo_epi64(rmax, z);
	rmax = _mm_max_epi16(a, b);
	// 結果取り出し
	int val_min = _mm_extract_epi16(rmin, 0);
	int val_max = _mm_extract_epi16(rmax, 0);

	return val_max - val_min;
}
