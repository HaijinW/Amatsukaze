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
#include "Transcode.hpp"


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
	int audioSamplesPerFrame;
	bool interlaced;

	InputContext inputCtx;
	CodecContext codecCtx;

	AVStream *videoStream;

	std::unique_ptr<AMTSourceData> storage;

	struct CacheFrame {
		PVideoFrame data;
		TreeNode<int, CacheFrame*> treeNode;
		ListNode<CacheFrame*> listNode;
	};

	Tree<int, CacheFrame*> frameCache;
	List<CacheFrame*> recentAccessed;

	// �f�R�[�h�ł��Ȃ������t���[�����X�g
	std::set<int> failedFrames;

	VideoInfo vi;

	std::mutex mutex;

	File waveFile;

	bool initialized;

	int seekDistance;

	// OnFrameDecoded�Œ��O�Ƀf�R�[�h���ꂽ�t���[��
	// �܂��f�R�[�h���ĂȂ��ꍇ��-1
	int lastDecodeFrame;

	// codecCtx�����O�Ƀf�R�[�h�����t���[���ԍ�
	// �܂��f�R�[�h���ĂȂ��ꍇ��nullptr
	std::unique_ptr<Frame> prevFrame;

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

	void MakeCodecContext() {
		AVCodecID vcodecId = videoStream->codecpar->codec_id;
		AVCodec *pCodec = getHWAccelCodec(vcodecId);
		if (pCodec == NULL) {
			ctx.warn("�w�肳�ꂽ�f�R�[�_���g�p�ł��Ȃ����߃f�t�H���g�f�R�[�_���g���܂�");
			pCodec = avcodec_find_decoder(vcodecId);
		}
		if (pCodec == NULL) {
			THROW(FormatException, "Could not find decoder ...");
		}
		codecCtx.Set(pCodec);
		if (avcodec_parameters_to_context(codecCtx(), videoStream->codecpar) != 0) {
			THROW(FormatException, "avcodec_parameters_to_context failed");
		}
		codecCtx()->thread_count = GetProcessorCount();
		if (avcodec_open2(codecCtx(), pCodec, NULL) != 0) {
			THROW(FormatException, "avcodec_open2 failed");
		}
	}

	void MakeVideoInfo(const VideoFormat& vfmt, const AudioFormat& afmt) {
		vi.width = vfmt.width;
		vi.height = vfmt.height;
		vi.SetFPS(vfmt.frameRateNum, vfmt.frameRateDenom);
		vi.num_frames = int(frames.size());

		interlaced = !vfmt.progressive;

		// �r�b�g�[�x�͎擾���ĂȂ��̂Ńt���[�����f�R�[�h���Ď擾����
		//vi.pixel_type = VideoInfo::CS_YV12;
		
		if (audioFrames.size() > 0) {
			audioSamplesPerFrame = 1024;
			// waveLength�̓[���̂��Ƃ�����̂Œ���
			for (int i = 0; i < (int)audioFrames.size(); ++i) {
				if (audioFrames[i].waveLength != 0) {
					audioSamplesPerFrame = audioFrames[i].waveLength / 4; // 16bit�X�e���I�O��
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

	void ResetDecoder() {
		lastDecodeFrame = -1;
		prevFrame = nullptr;
		//if (codecCtx() == nullptr) {
			MakeCodecContext();
		//}
		//avcodec_flush_buffers(codecCtx());
	}

	template <typename T, int step>
	void Copy(T* dst, const T* top, const T* bottom, int w, int h, int dpitch, int tpitch, int bpitch)
	{
		for (int y = 0; y < h; y += 2) {
			T* dst0 = dst + dpitch * (y + 0);
			T* dst1 = dst + dpitch * (y + 1);
			const T* src0 = top + tpitch * (y + 0);
			const T* src1 = bottom + bpitch * (y + 1);
			for (int x = 0; x < w; ++x) {
				dst0[x] = src0[x * step];
				dst1[x] = src1[x * step];
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

		Copy<T, 1>(dstY, srctY, srcbY, vi.width, vi.height, dstPitchY, srctPitchY, srcbPitchY);

		int widthUV = vi.width >> desc->log2_chroma_w;
		int heightUV = vi.height >> desc->log2_chroma_h;
		if (top->format != AV_PIX_FMT_NV12) {
			Copy<T, 1>(dstU, srctU, srcbU, widthUV, heightUV, dstPitchUV, srctPitchUV, srcbPitchUV);
			Copy<T, 1>(dstV, srctV, srcbV, widthUV, heightUV, dstPitchUV, srctPitchUV, srcbPitchUV);
		}
		else {
			Copy<T, 2>(dstU, srctU, srcbU, widthUV, heightUV, dstPitchUV, srctPitchUV, srcbPitchUV);
			Copy<T, 2>(dstV, srctV, srcbV, widthUV, heightUV, dstPitchUV, srctPitchUV, srcbPitchUV);
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
			// �L���b�V�������ꂽ��폜
			CacheFrame* pdel = recentAccessed.back().value;
			frameCache.erase(frameCache.it(&pdel->treeNode));
			recentAccessed.erase(recentAccessed.it(&pdel->listNode));
			delete pdel;
		}
	}

	void OnFrameDecoded(Frame& frame, IScriptEnvironment* env) {

		if (initialized == false) {
			// ������

			// �r�b�g�[�x�擾
			const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get((AVPixelFormat)(frame()->format));
			switch (desc->comp[0].depth) {
			case 8:
				vi.pixel_type = VideoInfo::CS_YV12;
				break;
			case 10:
				vi.pixel_type = VideoInfo::CS_YUV420P10;
				break;
			case 12:
				vi.pixel_type = VideoInfo::CS_YUV420P12;
				break;
			default:
				env->ThrowError("�Ή����Ă��Ȃ��r�b�g�[�x�ł�");
				break;
			}

			initialized = true;
		}

		// ffmpeg��pts wrap�̎d������Ȃ̂ŉ���33bit�݂̂�����
		//�i26���Ԉȏ゠�铮�悾�Əd������\���͂��邪�����j
		int64_t pts = frame()->pts & ((int64_t(1) << 33) - 1);
		auto it = std::lower_bound(frames.begin(), frames.end(), pts, [](const FilterSourceFrame& e, int64_t pts) {
			return e.framePTS < pts;
		});

		if (it == frames.begin() && it->framePTS != pts) {
			// �����������ꍇ��1�����ǉ����Č���
			pts += (int64_t(1) << 33);
			it = std::lower_bound(frames.begin(), frames.end(), pts, [](const FilterSourceFrame& e, int64_t pts) {
				return e.framePTS < pts;
			});
		}

		if (it == frames.end()) {
			// �Ō����낾����
			lastDecodeFrame = vi.num_frames;
			prevFrame = nullptr; // �A���łȂ��Ȃ�ꍇ��null���Z�b�g
			return;
		}

		if (it->framePTS != pts) {
			// ��v����t���[�����Ȃ�
			ctx.incrementCounter("incident");
			ctx.warn("Unknown PTS frame %lld", pts);
			prevFrame = nullptr; // �A���łȂ��Ȃ�ꍇ��null���Z�b�g
			return;
		}

		int frameIndex = int(it - frames.begin());
		auto cacheit = frameCache.find(frameIndex);

		lastDecodeFrame = frameIndex;

		if (it->halfDelay) {
			// �f�B���C��K�p������
			if (cacheit != frameCache.end()) {
				// ���łɃL���b�V���ɂ���
				UpdateAccessed(cacheit->value);
			}
			else if (prevFrame != nullptr) {
				PutFrame(frameIndex, MakeFrame((*prevFrame)(), frame(), env));
			}

			// ���̃t���[���������t���[�����Q�Ƃ��Ă��炻����o��
			auto next = it + 1;
			if (next != frames.end() && next->framePTS == it->framePTS) {
				auto cachenext = frameCache.find(frameIndex + 1);
				if (cachenext != frameCache.end()) {
					// ���łɃL���b�V���ɂ���
					UpdateAccessed(cachenext->value);
				}
				else {
					PutFrame(frameIndex + 1, MakeFrame(frame(), frame(), env));
				}
				lastDecodeFrame = frameIndex + 1;
			}
		}
		else {
			// ���̂܂�
			if (cacheit != frameCache.end()) {
				// ���łɃL���b�V���ɂ���
				UpdateAccessed(cacheit->value);
			}
			else {
				PutFrame(frameIndex, MakeFrame(frame(), frame(), env));
			}
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
					THROW(FormatException, "avcodec_send_packet failed");
				}
				while (avcodec_receive_frame(codecCtx(), frame()) == 0) {
					// �ŏ���I�t���[���܂ŃX�L�b�v
					if (lastDecodeFrame != -1 || frame()->key_frame) {
						OnFrameDecoded(frame, env);
					}
				}
			}
			av_packet_unref(&packet);
			if (lastDecodeFrame >= goal) {
				break;
			}
		}
	}

	void registerFailedFrames(int begin, int end, IScriptEnvironment* env)
	{
		for (int f = begin; f < end; ++f) {
			failedFrames.insert(f);
		}
		// �f�R�[�h�s�t���[�������P���𒴂���ꍇ�̓G���[�Ƃ���
		if (failedFrames.size() * 10 > frames.size()) {
			env->ThrowError("[AMTSource] �f�R�[�h�ł��Ȃ��t���[�������������܂� -> %d�t���[�����f�R�[�h�s��",
				(int)failedFrames.size());
		}
	}

public:
	AMTSource(AMTContext& ctx,
		const std::string& srcpath,
		const std::string& audiopath,
		const VideoFormat& vfmt, const AudioFormat& afmt,
		const std::vector<FilterSourceFrame>& frames,
		const std::vector<FilterAudioFrame>& audioFrames,
		const DecoderSetting& decoderSetting,
		IScriptEnvironment* env)
		: AMTObject(ctx)
		, frames(frames)
		, audioFrames(audioFrames)
		, inputCtx(srcpath)
		, vi()
		, waveFile(audiopath, "rb")
		, seekDistance(10)
		, initialized(false)
		, lastDecodeFrame(-1)
	{
		MakeVideoInfo(vfmt, afmt);

		if (avformat_find_stream_info(inputCtx(), NULL) < 0) {
			THROW(FormatException, "avformat_find_stream_info failed");
		}
		videoStream = GetVideoStream(inputCtx());
		if (videoStream == NULL) {
			THROW(FormatException, "Could not find video stream ...");
		}

		ResetDecoder();
		DecodeLoop(0, env);
	}

	~AMTSource() {
		// �L���b�V�����폜
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

		// �L���b�V���ɂ���ΕԂ�
		auto it = frameCache.find(n);
		if (it != frameCache.end()) {
			UpdateAccessed(it->value);
			return it->value->data;
		}

		// �f�R�[�h�ł��Ȃ��t���[���͒��߂�
		if (failedFrames.find(n) != failedFrames.end()) {
			return ForceGetFrame(n, env);
		}

		// �L���b�V���ɂȂ��̂Ńf�R�[�h����
		if (lastDecodeFrame != -1 && n > lastDecodeFrame && n < lastDecodeFrame + seekDistance) {
			// �O�ɂ����߂�
			DecodeLoop(n, env);
		}
		else {
			// �V�[�N���ăf�R�[�h����
			int keyNum = frames[n].keyFrame;
			for (int i = 0; ; ++i) {
				int64_t fileOffset = frames[keyNum].fileOffset / 188 * 188;
				if (av_seek_frame(inputCtx(), -1, fileOffset, AVSEEK_FLAG_BYTE) < 0) {
					THROW(FormatException, "av_seek_frame failed");
				}
				ResetDecoder();
				DecodeLoop(n, env);
				if (frameCache.find(n) != frameCache.end()) {
					// �f�R�[�h����
					seekDistance = std::max(seekDistance, n - keyNum);
					break;
				}
				if (keyNum <= 0) {
					// ����ȏ�߂�Ȃ�
					// n����lastDecodeFrame�܂ł��f�R�[�h�s�Ƃ���
					registerFailedFrames(n, lastDecodeFrame, env);
					break;
				}
				if (lastDecodeFrame >= 0 && lastDecodeFrame < n) {
					// �f�[�^������Ȃ��ăS�[���ɓ��B�ł��Ȃ�����
					// ���̃t���[�������͑S�ăf�R�[�h�s�Ƃ���
					registerFailedFrames(lastDecodeFrame + 1, (int)frames.size(), env);
					break;
				}
				if (i == 2) {
					// �f�R�[�h���s
					// n����lastDecodeFrame�܂ł��f�R�[�h�s�Ƃ���
					registerFailedFrames(n, lastDecodeFrame, env);
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

		const int sampleBytes = 4; // 16bit�X�e���I�O��
		int frameWaveLength = audioSamplesPerFrame * sampleBytes;
		uint8_t* ptr = (uint8_t*)buf;
		for(__int64 frameIndex = start / audioSamplesPerFrame, frameOffset = start % audioSamplesPerFrame;
			count > 0 && frameIndex < (__int64)audioFrames.size();
			++frameIndex, frameOffset = 0)
		{
			// ���̃t���[���Ŗ��߂�ׂ��o�C�g��
			int readBytes = std::min<int>(
				(int)(frameWaveLength - frameOffset * sampleBytes),
				(int)count * sampleBytes);

			if (audioFrames[(size_t)frameIndex].waveLength != 0) {
				// wave������Ȃ�ǂ�
				waveFile.seek(audioFrames[(size_t)frameIndex].waveOffset + frameOffset * sampleBytes, SEEK_SET);
				waveFile.read(MemoryChunk(ptr, readBytes));
			}
			else {
				// �Ȃ��ꍇ�̓[�����߂���
				memset(ptr, 0x00, readBytes);
			}

			ptr += readBytes;
			count -= readBytes / sampleBytes;
		}
		if (count > 0) {
			// �t�@�C���̏I���܂œ��B������c��̓[���Ŗ��߂�
			memset(ptr, 0, (size_t)count * sampleBytes);
		}
	}

	const VideoInfo& __stdcall GetVideoInfo() { return vi; }

	bool __stdcall GetParity(int n) {
		return interlaced;
	}
	
	int __stdcall SetCacheHints(int cachehints, int frame_range)
	{
		// ���ڃC���X�^���X�������ꍇ�AMTGuard������Ȃ��̂�MT_NICE_FILTER�ȊO�_��
		if (cachehints == CACHE_GET_MTMODE) return MT_NICE_FILTER;
		return 0;
	};
};

AMTContext* g_ctx_for_plugin_filter = nullptr;

void SaveAMTSource(
	const std::string& savepath,
	const std::string& srcpath,
	const std::string& audiopath,
	const VideoFormat& vfmt, const AudioFormat& afmt,
	const std::vector<FilterSourceFrame>& frames,
	const std::vector<FilterAudioFrame>& audioFrames,
	const DecoderSetting& decoderSetting)
{
	File file(savepath, "wb");
	file.writeArray(std::vector<char>(srcpath.begin(), srcpath.end()));
	file.writeArray(std::vector<char>(audiopath.begin(), audiopath.end()));
	file.writeValue(vfmt);
	file.writeValue(afmt);
	file.writeArray(frames);
	file.writeArray(audioFrames);
	file.writeValue(decoderSetting);
}

PClip LoadAMTSource(const std::string& loadpath, IScriptEnvironment* env)
{
	File file(loadpath, "rb");
	auto& srcpathv = file.readArray<char>();
	std::string srcpath(srcpathv.begin(), srcpathv.end());
	auto& audiopathv = file.readArray<char>();
	std::string audiopath(audiopathv.begin(), audiopathv.end());
	VideoFormat vfmt = file.readValue<VideoFormat>();
	AudioFormat afmt = file.readValue<AudioFormat>();
	auto data = std::unique_ptr<AMTSourceData>(new AMTSourceData());
	data->frames = file.readArray<FilterSourceFrame>();
	data->audioFrames = file.readArray<FilterAudioFrame>();
	DecoderSetting decoderSetting = file.readValue<DecoderSetting>();
	AMTSource* src = new AMTSource(*g_ctx_for_plugin_filter,
		srcpath, audiopath, vfmt, afmt, data->frames, data->audioFrames, decoderSetting, env);
	src->TransferStreamInfo(std::move(data));
	return src;
}

AVSValue CreateAMTSource(AVSValue args, void* user_data, IScriptEnvironment* env)
{
	if (g_ctx_for_plugin_filter == nullptr) {
		g_ctx_for_plugin_filter = new AMTContext();
	}
	std::string filename = args[0].AsString();
	return LoadAMTSource(filename, env);
}

class AVSLosslessSource : public IClip
{
	LosslessVideoFile file;
	CCodecPointer codec;
	VideoInfo vi;
	std::unique_ptr<uint8_t[]> codedFrame;
	std::unique_ptr<uint8_t[]> rawFrame;
public:
	AVSLosslessSource(AMTContext& ctx, const std::string& filepath, const VideoFormat& format, IScriptEnvironment* env)
		: file(ctx, filepath, "rb")
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
