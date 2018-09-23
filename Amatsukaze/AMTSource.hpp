/**
* Amtasukaze Avisynth Source Plugin
* Copyright (c) 2017-2018 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include <windows.h>

#include "avisynth.h"
#pragma comment(lib, "avisynth.lib")

#include <memory>
#include <mutex>
#include <set>

#include "Tree.hpp"
#include "List.hpp"


namespace av {

struct FakeAudioSample {

	enum {
		MAGIC = 0xFACE0D10,
		VERSION = 1
	};

	int32_t magic;
	int32_t version;
	int64_t index;
};

struct AMTSourceData {
	std::vector<FilterSourceFrame> frames;
	std::vector<FilterAudioFrame> audioFrames;
};

class AMTSource : public IClip, AMTObject
{
	const std::vector<FilterSourceFrame>& frames;
	const std::vector<FilterAudioFrame>& audioFrames;
	DecoderSetting decoderSetting;
	std::string filterdesc;
	int audioSamplesPerFrame;
	bool interlaced;

	bool outputQP; // QPテーブルを出力するか

	InputContext inputCtx;
	CodecContext codecCtx;

#if ENABLE_FFMPEG_FILTER
	FilterGraph filterGraph;
	AVFilterContext* bufferSrcCtx;
	AVFilterContext* bufferSinkCtx;
#endif

	AVStream *videoStream;

	std::unique_ptr<AMTSourceData> storage;

	struct CacheFrame {
		PVideoFrame data;
		TreeNode<int, CacheFrame*> treeNode;
		ListNode<CacheFrame*> listNode;
	};

	Tree<int, CacheFrame*> frameCache;
	List<CacheFrame*> recentAccessed;

	// デコードできなかったフレームの置換先リスト
	std::map<int, int> failedMap;

	VideoInfo vi;

	std::mutex mutex;

	File waveFile;

	int seekDistance;

	// OnFrameDecodedで直前にデコードされたフレーム
	// まだデコードしてない場合は-1
	int lastDecodeFrame;

	// codecCtxが直前にデコードしたフレーム番号
	// まだデコードしてない場合はnullptr
	std::unique_ptr<Frame> prevFrame;

	// 直前のnon B QPテーブル
	PVideoFrame nonBQPTable;

	AVCodec* getHWAccelCodec(AVCodecID vcodecId)
	{
		switch (vcodecId) {
		case AV_CODEC_ID_MPEG2VIDEO:
			switch (decoderSetting.mpeg2) {
			case DECODER_QSV:
				return avcodec_find_decoder_by_name("mpeg2_qsv");
			case DECODER_CUVID:
				return avcodec_find_decoder_by_name("mpeg2_cuvid");
			}
			break;
		case AV_CODEC_ID_H264:
			switch (decoderSetting.h264) {
			case DECODER_QSV:
				return avcodec_find_decoder_by_name("h264_qsv");
			case DECODER_CUVID:
				return avcodec_find_decoder_by_name("h264_cuvid");
			}
			break;
		case AV_CODEC_ID_HEVC:
			switch (decoderSetting.hevc) {
			case DECODER_QSV:
				return avcodec_find_decoder_by_name("hevc_qsv");
			case DECODER_CUVID:
				return avcodec_find_decoder_by_name("hevc_cuvid");
			}
			break;
		}
		return avcodec_find_decoder(vcodecId);
	}

	void MakeCodecContext(IScriptEnvironment* env) {
		AVCodecID vcodecId = videoStream->codecpar->codec_id;
		AVCodec *pCodec = getHWAccelCodec(vcodecId);
		if (pCodec == NULL) {
			ctx.warn("指定されたデコーダが使用できないためデフォルトデコーダを使います");
			pCodec = avcodec_find_decoder(vcodecId);
		}
		if (pCodec == NULL) {
			env->ThrowError("Could not find decoder ...");
		}
		codecCtx.Set(pCodec);
		if (avcodec_parameters_to_context(codecCtx(), videoStream->codecpar) != 0) {
			env->ThrowError("avcodec_parameters_to_context failed");
		}
		codecCtx()->thread_count = GetFFmpegThreads(GetProcessorCount());

		// export_mvs for codecview
		//AVDictionary *opts = NULL;
		//av_dict_set(&opts, "flags2", "+export_mvs", 0);
		
		if (avcodec_open2(codecCtx(), pCodec, NULL) != 0) {
			env->ThrowError("avcodec_open2 failed");
		}
	}

#if ENABLE_FFMPEG_FILTER
	void MakeFilterGraph(IScriptEnvironment* env) {
		char args[512];
		const AVFilter *buffersrc = avfilter_get_by_name("buffer");
		const AVFilter *buffersink = avfilter_get_by_name("buffersink");
		FilterInOut outputs;
		FilterInOut inputs;
		AVRational time_base = videoStream->time_base;

		filterGraph.Create();
		bufferSrcCtx = nullptr;
		bufferSinkCtx = nullptr;

		filterGraph()->nb_threads = 4;

		/* buffer video source: the decoded frames from the decoder will be inserted here. */
		snprintf(args, sizeof(args),
			"video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
			codecCtx()->width, codecCtx()->height, codecCtx()->pix_fmt,
			time_base.num, time_base.den,
			codecCtx()->sample_aspect_ratio.num, codecCtx()->sample_aspect_ratio.den);

		if (avfilter_graph_create_filter(&bufferSrcCtx, buffersrc, "in",
			args, NULL, filterGraph()) < 0) {
			env->ThrowError("avfilter_graph_create_filter failed (Cannot create buffer source)");
		}

		/* buffer video sink: to terminate the filter chain. */
		if (avfilter_graph_create_filter(&bufferSinkCtx, buffersink, "out",
			NULL, NULL, filterGraph()) < 0) {
			env->ThrowError("avfilter_graph_create_filter failed (Cannot create buffer sink)");
		}

		if (av_opt_set_bin(bufferSinkCtx, "pix_fmts",
			(uint8_t*)&codecCtx()->pix_fmt, sizeof(codecCtx()->pix_fmt),
			AV_OPT_SEARCH_CHILDREN) < 0) {
			env->ThrowError("av_opt_set_bin failed (cannot set output pixel format)");
		}

		/*
		* Set the endpoints for the filter graph. The filter_graph will
		* be linked to the graph described by filters_descr.
		*/

		/*
		* The buffer source output must be connected to the input pad of
		* the first filter described by filters_descr; since the first
		* filter input label is not specified, it is set to "in" by
		* default.
		*/
		outputs()->name = av_strdup("in");
		outputs()->filter_ctx = bufferSrcCtx;
		outputs()->pad_idx = 0;
		outputs()->next = NULL;

		/*
		* The buffer sink input must be connected to the output pad of
		* the last filter described by filters_descr; since the last
		* filter output label is not specified, it is set to "out" by
		* default.
		*/
		inputs()->name = av_strdup("out");
		inputs()->filter_ctx = bufferSinkCtx;
		inputs()->pad_idx = 0;
		inputs()->next = NULL;

		if (avfilter_graph_parse_ptr(filterGraph(), filterdesc.c_str(),
			&inputs(), &outputs(), NULL) < 0) {
			env->ThrowError("avfilter_graph_parse_ptr failed");
		}

		if (avfilter_graph_config(filterGraph(), NULL) < 0) {
			env->ThrowError("avfilter_graph_config failed");
		}
	}
#endif

	void MakeVideoInfo(const VideoFormat& vfmt, const AudioFormat& afmt) {
		vi.width = vfmt.width;
		vi.height = vfmt.height;
		vi.SetFPS(vfmt.frameRateNum, vfmt.frameRateDenom);
		vi.num_frames = int(frames.size());

		interlaced = !vfmt.progressive;

		if (audioFrames.size() > 0) {
			audioSamplesPerFrame = 1024;
			// waveLengthはゼロのこともあるので注意
			for (int i = 0; i < (int)audioFrames.size(); ++i) {
				if (audioFrames[i].waveLength != 0) {
					audioSamplesPerFrame = audioFrames[i].waveLength / 4; // 16bitステレオ前提
					break;
				}
			}
			vi.audio_samples_per_second = afmt.sampleRate;
			vi.sample_type = SAMPLE_INT16;
			vi.num_audio_samples = audioSamplesPerFrame * audioFrames.size();
			vi.nchannels = 2;
		}
		else {
			// No audio
			vi.audio_samples_per_second = 0;
			vi.num_audio_samples = 0;
			vi.nchannels = 0;
		}
	}

	void UpdateVideoInfo(IScriptEnvironment* env)
	{
		// ビット深度は取得してないのでffmpegから取得する
		vi.pixel_type = toAVSFormat(codecCtx()->pix_fmt, env);

#if ENABLE_FFMPEG_FILTER
		if (bufferSinkCtx) {
			// フィルタがあればフィルタの出力に更新
			const AVFilterLink* outlink = bufferSinkCtx->inputs[0];
			vi.pixel_type = toAVSFormat((AVPixelFormat)outlink->format, env);

			if (outlink->w != vi.width ||
				outlink->h != vi.height) {
				env->ThrowError("ffmpeg filter output is resized, which is not supported on current AMTSource.");
			}
		}
#endif
	}

	void ResetDecoder(IScriptEnvironment* env) {
		lastDecodeFrame = -1;
		prevFrame = nullptr;
		MakeCodecContext(env);
#if ENABLE_FFMPEG_FILTER
		if (filterdesc.size()) {
			MakeFilterGraph(env);
		}
#endif
	}

	template <typename T>
	void Copy1(T* dst, const T* top, const T* bottom, int w, int h, int dpitch, int tpitch, int bpitch)
	{
		for (int y = 0; y < h; y += 2) {
			T* dst0 = dst + dpitch * (y + 0);
			T* dst1 = dst + dpitch * (y + 1);
			const T* src0 = top + tpitch * (y + 0);
			const T* src1 = bottom + bpitch * (y + 1);
			memcpy(dst0, src0, sizeof(T) * w);
			memcpy(dst1, src1, sizeof(T) * w);
		}
	}

	template <typename T>
	void Copy2(T* dstU, T* dstV, const T* top, const T* bottom, int w, int h, int dpitch, int tpitch, int bpitch)
	{
		for (int y = 0; y < h; y += 2) {
			T* dstU0 = dstU + dpitch * (y + 0);
			T* dstU1 = dstU + dpitch * (y + 1);
			T* dstV0 = dstV + dpitch * (y + 0);
			T* dstV1 = dstV + dpitch * (y + 1);
			const T* src0 = top + tpitch * (y + 0);
			const T* src1 = bottom + bpitch * (y + 1);
			for (int x = 0; x < w; ++x) {
				dstU0[x] = src0[x * 2 + 0];
				dstV0[x] = src0[x * 2 + 1];
				dstU1[x] = src1[x * 2 + 0];
				dstV1[x] = src1[x * 2 + 1];
			}
		}
	}

	template <typename T>
	void MergeField(PVideoFrame& dst, AVFrame* top, AVFrame* bottom) {
		const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get((AVPixelFormat)(top->format));

		T* srctY = (T*)top->data[0];
		T* srctU = (T*)top->data[1];
		T* srctV = (top->format != AV_PIX_FMT_NV12) ? (T*)top->data[2] : ((T*)top->data[1] + 1);
		T* srcbY = (T*)bottom->data[0];
		T* srcbU = (T*)bottom->data[1];
		T* srcbV = (top->format != AV_PIX_FMT_NV12) ? (T*)bottom->data[2] : ((T*)bottom->data[1] + 1);
		T* dstY = (T*)dst->GetWritePtr(PLANAR_Y);
		T* dstU = (T*)dst->GetWritePtr(PLANAR_U);
		T* dstV = (T*)dst->GetWritePtr(PLANAR_V);

		int srctPitchY = top->linesize[0];
		int srctPitchUV = top->linesize[1];
		int srcbPitchY = bottom->linesize[0];
		int srcbPitchUV = bottom->linesize[1];
		int dstPitchY = dst->GetPitch(PLANAR_Y);
		int dstPitchUV = dst->GetPitch(PLANAR_U);

		Copy1<T>(dstY, srctY, srcbY, vi.width, vi.height, dstPitchY, srctPitchY, srcbPitchY);

		int widthUV = vi.width >> desc->log2_chroma_w;
		int heightUV = vi.height >> desc->log2_chroma_h;
		if (top->format != AV_PIX_FMT_NV12) {
			Copy1<T>(dstU, srctU, srcbU, widthUV, heightUV, dstPitchUV, srctPitchUV, srcbPitchUV);
			Copy1<T>(dstV, srctV, srcbV, widthUV, heightUV, dstPitchUV, srctPitchUV, srcbPitchUV);
		}
		else {
			Copy2<T>(dstU, dstV, srctU, srcbU, widthUV, heightUV, dstPitchUV, srctPitchUV, srcbPitchUV);
		}
	}

	PVideoFrame MakeFrame(AVFrame* top, AVFrame* bottom, IScriptEnvironment* env) {
		PVideoFrame ret = env->NewVideoFrame(vi);
		const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get((AVPixelFormat)(top->format));

		if (desc->comp[0].depth > 8) {
			MergeField<uint16_t>(ret, top, bottom);
		}
		else {
			MergeField<uint8_t>(ret, top, bottom);
		}

		// フレームタイプ
		ret->SetProperty("FrameType", top->pict_type);

		// QPテーブル
		if (outputQP) {
			int qp_stride, qp_scale_type;
			const int8_t* qp_table = av_frame_get_qp_table(top, &qp_stride, &qp_scale_type);
			if (qp_table) {
				VideoInfo qpvi = vi;
				if (!qp_stride) {
					qpvi.width = (vi.width + 15) >> 4;
					qpvi.height = 1;
				}
				else {
					qpvi.width = qp_stride;
					qpvi.height = (vi.height + 15) >> 4;
				}
				qpvi.pixel_type = VideoInfo::CS_Y8;
				PVideoFrame qpframe = env->NewVideoFrame(qpvi);
				env->BitBlt(qpframe->GetWritePtr(), qpframe->GetPitch(), (const BYTE*)qp_table, qpvi.width, qpvi.width, qpvi.height);
				if (top->pict_type != AV_PICTURE_TYPE_B) {
					nonBQPTable = qpframe;
				}
				ret->SetProperty("QP_Table", qpframe);
				ret->SetProperty("QP_Table_Non_B", nonBQPTable);
				ret->SetProperty("QP_Stride", qp_stride ? qpframe->GetPitch() : 0);
				ret->SetProperty("QP_ScaleType", qp_scale_type);
			}
		}

		return ret;
	}

	void PutFrame(int n, const PVideoFrame& frame) {
		CacheFrame* pcache = new CacheFrame();
		pcache->data = frame;
		pcache->treeNode.key = n;
		pcache->treeNode.value = pcache;
		pcache->listNode.value = pcache;
		frameCache.insert(&pcache->treeNode);
		recentAccessed.push_front(&pcache->listNode);

		if ((int)recentAccessed.size() > seekDistance * 3 / 2) {
			// キャッシュから溢れたら削除
			CacheFrame* pdel = recentAccessed.back().value;
			frameCache.erase(frameCache.it(&pdel->treeNode));
			recentAccessed.erase(recentAccessed.it(&pdel->listNode));
			delete pdel;
		}
	}

	int toAVSFormat(AVPixelFormat format, IScriptEnvironment* env)
	{
		// ビット深度取得
		const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(format);
		switch (desc->comp[0].depth) {
		case 8:
			return VideoInfo::CS_YV12;
		case 10:
			return VideoInfo::CS_YUV420P10;
		case 12:
			return VideoInfo::CS_YUV420P12;
		}
		env->ThrowError("対応していないビット深度です");
		return 0;
	}

#if ENABLE_FFMPEG_FILTER
	void InputFrameFilter(Frame* frame, bool enableOut, IScriptEnvironment* env)
	{
		/* push the decoded frame into the filtergraph */
		if (av_buffersrc_add_frame_flags(bufferSrcCtx, frame ? (*frame)() : nullptr, 0) < 0) {
			env->ThrowError("av_buffersrc_add_frame_flags failed (Error while feeding the filtergraph)");
		}

		/* pull filtered frames from the filtergraph */
		while (1) {
			Frame filtered;
			int ret = av_buffersink_get_frame(bufferSinkCtx, filtered());
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
				// もっと入力が必要 or もうフレームがない
				break;
			}
			if (ret < 0) {
				env->ThrowError("av_buffersink_get_frame failed");
			}
			if (enableOut) {
				OnFrameOutput(filtered, env);
			}
		}
	}

	void OnFrameDecoded(Frame& frame, IScriptEnvironment* env)
	{
		if (bufferSrcCtx) {
			// フィルタ処理
			//frame()->pts = frame()->best_effort_timestamp;
			InputFrameFilter(&frame, true, env);
		}
		else {
			OnFrameOutput(frame, env);
		}
	}
#endif

	void OnFrameOutput(Frame& frame, IScriptEnvironment* env)
	{
		// ffmpegのpts wrapの仕方が謎なので下位33bitのみを見る
		//（26時間以上ある動画だと重複する可能性はあるが無視）
		int64_t pts = frame()->pts & ((int64_t(1) << 33) - 1);

		int64_t headDiff = 0, tailDiff = 0;
		auto it = std::lower_bound(frames.begin(), frames.end(), pts, [](const FilterSourceFrame& e, int64_t pts) {
			return e.framePTS < pts;
		});

		if (it == frames.begin() && pts < it->framePTS) {
			headDiff = it->framePTS - pts;
			// 小さすぎた場合は1周分追加して見る
			pts += (int64_t(1) << 33);
			it = std::lower_bound(frames.begin(), frames.end(), pts, [](const FilterSourceFrame& e, int64_t pts) {
				return e.framePTS < pts;
			});
		}

		if (it == frames.end()) {
			// 最後より後ろだった
			tailDiff = pts - frames.back().framePTS;
			// 前の可能性もあるので、判定
			if (headDiff == 0 || headDiff > tailDiff) {
				lastDecodeFrame = vi.num_frames;
			}
			prevFrame = nullptr; // 連続でなくなる場合はnullリセット
			return;
		}

		if (it->framePTS != pts) {
			// 一致するフレームがない
			ctx.incrementCounter(AMT_ERR_UNKNOWN_PTS);
			ctx.warnF("Unknown PTS frame %lld", pts);
			prevFrame = nullptr; // 連続でなくなる場合はnullリセット
			return;
		}

		int frameIndex = int(it - frames.begin());
		auto cacheit = frameCache.find(frameIndex);

		if (it->halfDelay) {
			// ディレイを適用させる
			if (cacheit != frameCache.end()) {
				// すでにキャッシュにある
				UpdateAccessed(cacheit->value);
				lastDecodeFrame = frameIndex;
			}
			else if (prevFrame != nullptr) {
				PutFrame(frameIndex, MakeFrame((*prevFrame)(), frame(), env));
				lastDecodeFrame = frameIndex;
			}
			else {
				// 直前のフレームがないのでフレームを作れない
			}

			// 次のフレームも同じフレームを参照してたらそれも出力
			auto next = it + 1;
			if (next != frames.end() && next->framePTS == it->framePTS) {
				auto cachenext = frameCache.find(frameIndex + 1);
				if (cachenext != frameCache.end()) {
					// すでにキャッシュにある
					UpdateAccessed(cachenext->value);
				}
				else {
					PutFrame(frameIndex + 1, MakeFrame(frame(), frame(), env));
				}
				lastDecodeFrame = frameIndex + 1;
			}
		}
		else {
			// そのまま
			if (cacheit != frameCache.end()) {
				// すでにキャッシュにある
				UpdateAccessed(cacheit->value);
			}
			else {
				PutFrame(frameIndex, MakeFrame(frame(), frame(), env));
			}
			lastDecodeFrame = frameIndex;
		}

		prevFrame = std::unique_ptr<Frame>(new Frame(frame));
	}

	void UpdateAccessed(CacheFrame* frame) {
		recentAccessed.erase(recentAccessed.it(&frame->listNode));
		recentAccessed.push_front(&frame->listNode);
	}

	PVideoFrame ForceGetFrame(int n, IScriptEnvironment* env) {
		if (frameCache.size() == 0) {
			return env->NewVideoFrame(vi);
		}
		auto lb = frameCache.lower_bound(n);
		if (lb->key != n && lb != frameCache.begin()) {
			--lb;
		}
		UpdateAccessed(lb->value);
		return lb->value->data;
	}

	void DecodeLoop(int goal, IScriptEnvironment* env) {
		Frame frame;
		AVPacket packet = AVPacket();
		while (av_read_frame(inputCtx(), &packet) == 0) {
			if (packet.stream_index == videoStream->index) {
				if (avcodec_send_packet(codecCtx(), &packet) != 0) {
					ctx.incrementCounter(AMT_ERR_DECODE_PACKET_FAILED);
					ctx.warn("avcodec_send_packet failed");
				}
				while (avcodec_receive_frame(codecCtx(), frame()) == 0) {
					// 最初はIフレームまでスキップ
					if (lastDecodeFrame != -1 || frame()->key_frame) {
#if ENABLE_FFMPEG_FILTER
						OnFrameDecoded(frame, env);
#else
						OnFrameOutput(frame, env);
#endif
					}
				}
			}
			av_packet_unref(&packet);
			if (lastDecodeFrame >= goal) {
				return;
			}
		}
#if ENABLE_FFMPEG_FILTER
		if (bufferSrcCtx) {
			// ストリームは全て読み取ったのでフィルタをflush
			InputFrameFilter(nullptr, true, env);
		}
#endif
	}

	void registerFailedFrames(int begin, int end, int replace, IScriptEnvironment* env)
	{
		for (int f = begin; f < end; ++f) {
			failedMap[f] = replace;
		}
		// デコード不可フレーム数が１割を超える場合はエラーとする
		if (failedMap.size() * 10 > frames.size()) {
			env->ThrowError("[AMTSource] デコードできないフレーム数が多すぎます -> %dフレームがデコード不可",
				(int)failedMap.size());
		}
	}

public:
	AMTSource(AMTContext& ctx,
		const tstring& srcpath,
		const tstring& audiopath,
		const VideoFormat& vfmt, const AudioFormat& afmt,
		const std::vector<FilterSourceFrame>& frames,
		const std::vector<FilterAudioFrame>& audioFrames,
		const DecoderSetting& decoderSetting,
		const char* filterdesc,
		bool outputQP,
		IScriptEnvironment* env)
		: AMTObject(ctx)
		, frames(frames)
		, decoderSetting(decoderSetting)
		, audioFrames(audioFrames)
		, filterdesc(filterdesc)
		, outputQP(outputQP)
		, inputCtx(srcpath)
		, vi()
		, waveFile(audiopath, _T("rb"))
#if ENABLE_FFMPEG_FILTER
		, bufferSrcCtx()
		, bufferSinkCtx()
#endif
		, seekDistance(10)
		, lastDecodeFrame(-1)
	{
#if !ENABLE_FFMPEG_FILTER
		if (this->filterdesc.size()) {
			env->ThrowError("This AMTSouce build does not support FFmpeg filter option ...");
		}
#endif
		MakeVideoInfo(vfmt, afmt);

		if (avformat_find_stream_info(inputCtx(), NULL) < 0) {
			env->ThrowError("avformat_find_stream_info failed");
		}
		videoStream = GetVideoStream(inputCtx());
		if (videoStream == NULL) {
			env->ThrowError("Could not find video stream ...");
		}

		// 初期化
		ResetDecoder(env);
		UpdateVideoInfo(env);
	}

	~AMTSource() {
		// キャッシュを削除
		while (recentAccessed.size() > 0) {
			CacheFrame* pdel = recentAccessed.back().value;
			frameCache.erase(frameCache.it(&pdel->treeNode));
			recentAccessed.erase(recentAccessed.it(&pdel->listNode));
			delete pdel;
		}
	}

	void TransferStreamInfo(std::unique_ptr<AMTSourceData>&& streamInfo) {
		storage = std::move(streamInfo);
	}

	PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env)
	{
		std::lock_guard<std::mutex> guard(mutex);

		// キャッシュにあれば返す
		auto it = frameCache.find(n);
		if (it != frameCache.end()) {
			UpdateAccessed(it->value);
			return it->value->data;
		}

		// デコードできないフレームは置換フレームに置き換える
		if (failedMap.find(n) != failedMap.end()) {
			n = failedMap[n];
		}

		// キャッシュにないのでデコードする
		if (lastDecodeFrame != -1 && n > lastDecodeFrame && n < lastDecodeFrame + seekDistance) {
			// 前にすすめる
			DecodeLoop(n, env);
		}
		else {
			// シークしてデコードする
			int keyNum = frames[n].keyFrame;
			for (int i = 0; ; ++i) {
				int64_t fileOffset = frames[keyNum].fileOffset / 188 * 188;
				if (av_seek_frame(inputCtx(), -1, fileOffset, AVSEEK_FLAG_BYTE) < 0) {
					THROW(FormatException, "av_seek_frame failed");
				}
				ResetDecoder(env);
				DecodeLoop(n, env);
				if (frameCache.find(n) != frameCache.end()) {
					// デコード成功
					seekDistance = std::max(seekDistance, n - keyNum);
					break;
				}
				if (keyNum <= 0) {
					// これ以上戻れない
					// nからlastDecodeFrameまでをデコード不可とする
					registerFailedFrames(n, lastDecodeFrame, lastDecodeFrame, env);
					break;
				}
				if (lastDecodeFrame >= 0 && lastDecodeFrame < n) {
					// データが足りなくてゴールに到達できなかった
					// このフレームより後ろは全てデコード不可とする
					registerFailedFrames(lastDecodeFrame + 1, (int)frames.size(), lastDecodeFrame, env);
					break;
				}
				if (i == 2) {
					// デコード失敗
					// nからlastDecodeFrameまでをデコード不可とする
					registerFailedFrames(n, lastDecodeFrame, lastDecodeFrame, env);
					break;
				}
				keyNum -= std::max(5, keyNum - frames[keyNum - 1].keyFrame);
			}
		}

		return ForceGetFrame(n, env);
	}

	void __stdcall GetAudio(void* buf, __int64 start, __int64 count, IScriptEnvironment* env)
	{
		std::lock_guard<std::mutex> guard(mutex);

		if (audioFrames.size() == 0) return;

		const int sampleBytes = 4; // 16bitステレオ前提
		int frameWaveLength = audioSamplesPerFrame * sampleBytes;
		uint8_t* ptr = (uint8_t*)buf;
		for (__int64 frameIndex = start / audioSamplesPerFrame, frameOffset = start % audioSamplesPerFrame;
			count > 0 && frameIndex < (__int64)audioFrames.size();
			++frameIndex, frameOffset = 0)
		{
			// このフレームで埋めるべきバイト数
			int readBytes = std::min<int>(
				(int)(frameWaveLength - frameOffset * sampleBytes),
				(int)count * sampleBytes);

			if (audioFrames[(size_t)frameIndex].waveLength != 0) {
				// waveがあるなら読む
				waveFile.seek(audioFrames[(size_t)frameIndex].waveOffset + frameOffset * sampleBytes, SEEK_SET);
				waveFile.read(MemoryChunk(ptr, readBytes));
			}
			else {
				// ない場合はゼロ埋めする
				memset(ptr, 0x00, readBytes);
			}

			ptr += readBytes;
			count -= readBytes / sampleBytes;
		}
		if (count > 0) {
			// ファイルの終わりまで到達したら残りはゼロで埋める
			memset(ptr, 0, (size_t)count * sampleBytes);
		}
	}

	const VideoInfo& __stdcall GetVideoInfo() { return vi; }

	bool __stdcall GetParity(int n) {
		return interlaced;
	}

	int __stdcall SetCacheHints(int cachehints, int frame_range)
	{
		// 直接インスタンス化される場合、MTGuardが入らないのでMT_NICE_FILTER以外ダメ
		if (cachehints == CACHE_GET_MTMODE) return MT_NICE_FILTER;
		return 0;
	};
};

AMTContext* g_ctx_for_plugin_filter = nullptr;

void SaveAMTSource(
	const tstring& savepath,
	const tstring& srcpath,
	const tstring& audiopath,
	const VideoFormat& vfmt, const AudioFormat& afmt,
	const std::vector<FilterSourceFrame>& frames,
	const std::vector<FilterAudioFrame>& audioFrames,
	const DecoderSetting& decoderSetting)
{
	File file(savepath, _T("wb"));
	file.writeArray(std::vector<tchar>(srcpath.begin(), srcpath.end()));
	file.writeArray(std::vector<tchar>(audiopath.begin(), audiopath.end()));
	file.writeValue(vfmt);
	file.writeValue(afmt);
	file.writeArray(frames);
	file.writeArray(audioFrames);
	file.writeValue(decoderSetting);
}

PClip LoadAMTSource(const tstring& loadpath, const char* filterdesc, bool outputQP, IScriptEnvironment* env)
{
	File file(loadpath, _T("rb"));
	auto& srcpathv = file.readArray<tchar>();
	tstring srcpath(srcpathv.begin(), srcpathv.end());
	auto& audiopathv = file.readArray<tchar>();
	tstring audiopath(audiopathv.begin(), audiopathv.end());
	VideoFormat vfmt = file.readValue<VideoFormat>();
	AudioFormat afmt = file.readValue<AudioFormat>();
	auto data = std::unique_ptr<AMTSourceData>(new AMTSourceData());
	data->frames = file.readArray<FilterSourceFrame>();
	data->audioFrames = file.readArray<FilterAudioFrame>();
	DecoderSetting decoderSetting = file.readValue<DecoderSetting>();
	AMTSource* src = new AMTSource(*g_ctx_for_plugin_filter,
		srcpath, audiopath, vfmt, afmt, data->frames, data->audioFrames, decoderSetting, filterdesc, outputQP, env);
	src->TransferStreamInfo(std::move(data));
	return src;
}

AVSValue CreateAMTSource(AVSValue args, void* user_data, IScriptEnvironment* env)
{
	if (g_ctx_for_plugin_filter == nullptr) {
		g_ctx_for_plugin_filter = new AMTContext();
	}
	tstring filename = to_tstring(args[0].AsString());
	const char* filterdesc = args[1].AsString("");
	bool outputQP = args[2].AsBool(true);
	return LoadAMTSource(filename, filterdesc, outputQP, env);
}

class AVSLosslessSource : public IClip
{
	LosslessVideoFile file;
	CCodecPointer codec;
	VideoInfo vi;
	std::unique_ptr<uint8_t[]> codedFrame;
	std::unique_ptr<uint8_t[]> rawFrame;
public:
	AVSLosslessSource(AMTContext& ctx, const tstring& filepath, const VideoFormat& format, IScriptEnvironment* env)
		: file(ctx, filepath, _T("rb"))
		, codec(make_unique_ptr(CCodec::CreateInstance(UTVF_ULH0, "Amatsukaze")))
		, vi()
	{
		file.readHeader();
		vi.width = file.getWidth();
		vi.height = file.getHeight();
		_ASSERT(format.width == vi.width);
		_ASSERT(format.height == vi.height);
		vi.num_frames = file.getNumFrames();
		vi.pixel_type = VideoInfo::CS_YV12;
		vi.SetFPS(format.frameRateNum, format.frameRateDenom);
		auto extra = file.getExtra();
		if (codec->DecodeBegin(UTVF_YV12, vi.width, vi.height, CBGROSSWIDTH_WINDOWS, extra.data(), (int)extra.size())) {
			THROW(RuntimeException, "failed to DecodeBegin (UtVideo)");
		}

		size_t codedSize = codec->EncodeGetOutputSize(UTVF_YV12, vi.width, vi.height);
		codedFrame = std::unique_ptr<uint8_t[]>(new uint8_t[codedSize]);

		rawFrame = std::unique_ptr<uint8_t[]>(new uint8_t[vi.width * vi.height * 3 / 2]);
	}

	~AVSLosslessSource() {
		codec->DecodeEnd();
	}

	PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env)
	{
		n = std::max(0, std::min(vi.num_frames - 1, n));
		file.readFrame(n, codedFrame.get());
		codec->DecodeFrame(rawFrame.get(), codedFrame.get());
		PVideoFrame dst = env->NewVideoFrame(vi);
		CopyYV12(dst, rawFrame.get(), vi.width, vi.height);
		return dst;
	}

	void __stdcall GetAudio(void* buf, __int64 start, __int64 count, IScriptEnvironment* env) { return; }
	const VideoInfo& __stdcall GetVideoInfo() { return vi; }
	bool __stdcall GetParity(int n) { return false; }

	int __stdcall SetCacheHints(int cachehints, int frame_range)
	{
		if (cachehints == CACHE_GET_MTMODE) return MT_SERIALIZED;
		return 0;
	};
};

} // namespace av {
