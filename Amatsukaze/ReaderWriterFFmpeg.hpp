/**
* Reader/Writer with FFmpeg
* Copyright (c) 2017-2018 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>

#include "StreamUtils.hpp"
#include "ProcessThread.hpp"

// AMTSource�̃t�B���^�[�I�v�V������L���ɂ��邩
#define ENABLE_FFMPEG_FILTER 0

// libffmpeg
extern "C" {
#include <libavutil/imgutils.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#if ENABLE_FFMPEG_FILTER
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
#endif
}
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "swscale.lib")
#if ENABLE_FFMPEG_FILTER
#pragma comment(lib, "avfilter.lib")
#endif

namespace av {

int GetFFmpegThreads(int preferred) {
	return std::min(8, std::max(1, preferred));
}

AVStream* GetVideoStream(AVFormatContext* pCtx)
{
	for (int i = 0; i < (int)pCtx->nb_streams; ++i) {
		if (pCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			return pCtx->streams[i];
		}
	}
	return nullptr;
}

AVStream* GetVideoStream(AVFormatContext* ctx, int serviceid)
{
	for (int i = 0; i < (int)ctx->nb_programs; ++i) {
		if (ctx->programs[i]->program_num == serviceid) {
			auto prog = ctx->programs[i];
			for (int s = 0; s < (int)prog->nb_stream_indexes; ++s) {
				auto stream = ctx->streams[prog->stream_index[s]];
				if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
					return stream;
				}
			}
		}
	}
	return nullptr;
}

class Frame {
public:
	Frame()
		: frame_()
	{
		frame_ = av_frame_alloc();
	}
	Frame(const Frame& src) {
		frame_ = av_frame_alloc();
		av_frame_ref(frame_, src());
	}
	~Frame() {
		av_frame_free(&frame_);
	}
	AVFrame* operator()() {
		return frame_;
	}
	const AVFrame* operator()() const {
		return frame_;
	}
	Frame& operator=(const Frame& src) {
		av_frame_unref(frame_);
		av_frame_ref(frame_, src());
		return *this;
	}
private:
	AVFrame* frame_;
};

class CodecContext : NonCopyable {
public:
	CodecContext(AVCodec* pCodec)
		: ctx_()
	{
		Set(pCodec);
	}
	CodecContext()
		: ctx_()
	{ }
	~CodecContext() {
		Free();
	}
	void Set(AVCodec* pCodec) {
		if (pCodec == NULL) {
			THROW(RuntimeException, "pCodec is NULL");
		}
		Free();
		ctx_ = avcodec_alloc_context3(pCodec);
		if (ctx_ == NULL) {
			THROW(IOException, "failed avcodec_alloc_context3");
		}
	}
	void Free() {
		if (ctx_) {
			avcodec_free_context(&ctx_);
			ctx_ = NULL;
		}
	}
	AVCodecContext* operator()() {
		return ctx_;
	}
private:
	AVCodecContext *ctx_;
};

class InputContext : NonCopyable {
public:
	InputContext(const tstring& src)
		: ctx_()
	{
		if (avformat_open_input(&ctx_, to_string(src).c_str(), NULL, NULL) != 0) {
			THROW(IOException, "failed avformat_open_input");
		}
	}
	~InputContext() {
		avformat_close_input(&ctx_);
	}
	AVFormatContext* operator()() {
		return ctx_;
	}
private:
	AVFormatContext* ctx_;
};

#if ENABLE_FFMPEG_FILTER
class FilterGraph : NonCopyable {
public:
	FilterGraph()
		: ctx_()
	{ }
	~FilterGraph() {
		Free();
	}
	void Create() {
		Free();
		ctx_ = avfilter_graph_alloc();
		if (ctx_ == NULL) {
			THROW(IOException, "failed avfilter_graph_alloc");
		}
	}
	void Free() {
		if (ctx_) {
			avfilter_graph_free(&ctx_);
			ctx_ = NULL;
		}
	}
	AVFilterGraph* operator()() {
		return ctx_;
	}
private:
	AVFilterGraph* ctx_;
};

class FilterInOut : NonCopyable {
public:
	FilterInOut()
		: ctx_(avfilter_inout_alloc())
	{
		if (ctx_ == nullptr) {
			THROW(IOException, "failed avfilter_inout_alloc");
		}
	}
	~FilterInOut() {
		avfilter_inout_free(&ctx_);
	}
	AVFilterInOut*& operator()() {
		return ctx_;
	}
private:
	AVFilterInOut* ctx_;
};
#endif

class WriteIOContext : NonCopyable {
public:
	WriteIOContext(int bufsize)
		: ctx_()
	{
		unsigned char* buffer = (unsigned char*)av_malloc(bufsize);
		ctx_ = avio_alloc_context(buffer, bufsize, 1, this, NULL, write_packet_, NULL);
	}
	~WriteIOContext() {
		av_free(ctx_->buffer);
		av_free(ctx_);
	}
	AVIOContext* operator()() {
		return ctx_;
	}
protected:
	virtual void onWrite(MemoryChunk mc) = 0;
private:
	AVIOContext* ctx_;
	static int write_packet_(void *opaque, uint8_t *buf, int buf_size) {
		((WriteIOContext*)opaque)->onWrite(MemoryChunk(buf, buf_size));
		return 0;
	}
};

class OutputContext : NonCopyable {
public:
	OutputContext(WriteIOContext& ioCtx, const char* format)
		: ctx_()
	{
		if (avformat_alloc_output_context2(&ctx_, NULL, format, "-") < 0) {
			THROW(FormatException, "avformat_alloc_output_context2 failed");
		}
		if (ctx_->pb != NULL) {
			THROW(FormatException, "pb already has ...");
		}
		ctx_->pb = ioCtx();
		// 10bit�ȏ�YUV4MPEG�Ή�
		ctx_->strict_std_compliance = FF_COMPLIANCE_UNOFFICIAL;
	}
	~OutputContext() {
		avformat_free_context(ctx_);
	}
	AVFormatContext* operator()() {
		return ctx_;
	}
private:
	AVFormatContext* ctx_;
};

class VideoReader : AMTObject
{
public:
	VideoReader(AMTContext& ctx)
		: AMTObject(ctx)
		, fmt_()
		, fieldMode_()
	{ }

	void readAll(const tstring& src, const DecoderSetting& decoderSetting)
	{
		InputContext inputCtx(src);
		if (avformat_find_stream_info(inputCtx(), NULL) < 0) {
			THROW(FormatException, "avformat_find_stream_info failed");
		}
		onFileOpen(inputCtx());
		AVStream *videoStream = GetVideoStream(inputCtx());
		if (videoStream == NULL) {
			THROW(FormatException, "Could not find video stream ...");
		}
		AVCodecID vcodecId = videoStream->codecpar->codec_id;
		AVCodec *pCodec = getHWAccelCodec(vcodecId, decoderSetting);
		if (pCodec == NULL) {
			ctx.warn("�w�肳�ꂽ�f�R�[�_���g�p�ł��Ȃ����߃f�t�H���g�f�R�[�_���g���܂�");
			pCodec = avcodec_find_decoder(vcodecId);
		}
		if (pCodec == NULL) {
			THROW(FormatException, "Could not find decoder ...");
		}
		CodecContext codecCtx(pCodec);
		if (avcodec_parameters_to_context(codecCtx(), videoStream->codecpar) != 0) {
			THROW(FormatException, "avcodec_parameters_to_context failed");
		}
		if (avcodec_open2(codecCtx(), pCodec, NULL) != 0) {
			THROW(FormatException, "avcodec_open2 failed");
		}

		bool first = true;
		Frame frame;
		AVPacket packet = AVPacket();
		while (av_read_frame(inputCtx(), &packet) == 0) {
			if (packet.stream_index == videoStream->index) {
				if (avcodec_send_packet(codecCtx(), &packet) != 0) {
					THROW(FormatException, "avcodec_send_packet failed");
				}
				while (avcodec_receive_frame(codecCtx(), frame()) == 0) {
					if (first) {
						onFirstFrame(videoStream, frame());
						first = false;
					}
					onFrame(frame);
				}
			}
			else {
				onAudioPacket(packet);
			}
			av_packet_unref(&packet);
		}

		// flush decoder
		if (avcodec_send_packet(codecCtx(), NULL) != 0) {
			THROW(FormatException, "avcodec_send_packet failed");
		}
		while (avcodec_receive_frame(codecCtx(), frame()) == 0) {
			onFrame(frame);
		}

	}

protected:
	virtual void onFileOpen(AVFormatContext *fmt) { };
	virtual void onVideoFormat(AVStream *stream, VideoFormat fmt) { };
	virtual void onFrameDecoded(Frame& frame) { };
	virtual void onAudioPacket(AVPacket& packet) { };

private:
	VideoFormat fmt_;
	bool fieldMode_;
	std::unique_ptr<av::Frame> prevFrame_;

	AVCodec* getHWAccelCodec(AVCodecID vcodecId, const DecoderSetting& decoderSetting)
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

	void onFrame(Frame& frame) {
		if (fieldMode_) {
			if (frame()->interlaced_frame == false) {
				// �t���[�����C���^���[�X�łȂ������炻�̂܂܏o��
				prevFrame_ = nullptr;
				onFrameDecoded(frame);
			}
			else if (prevFrame_ == nullptr) {
				// �g�b�v�t�B�[���h�łȂ�������j��
				// �d�l���ǂ����͕s��������FFMPEG ver.3.2.2����
				// top_field_first=1: top field
				// top_field_first=0: bottom field
				// �ƂȂ��Ă���悤�ł���
				if (frame()->top_field_first) {
					prevFrame_ = std::unique_ptr<av::Frame>(new av::Frame(frame));
				}
				else {
					ctx.warn("�g�b�v�t�B�[���h��z�肵�Ă����������ł͂Ȃ������̂Ńt�B�[���h��j��");
				}
			}
			else {
				// 2���̃t�B�[���h������
				auto merged = mergeFields(*prevFrame_, frame);
				onFrameDecoded(*merged);
				prevFrame_ = nullptr;
			}
		}
		else {
			onFrameDecoded(frame);
		}
	}

	void onFirstFrame(AVStream *stream, AVFrame *frame)
	{
		VIDEO_STREAM_FORMAT srcFormat = VS_UNKNOWN;
		switch (stream->codecpar->codec_id) {
		case AV_CODEC_ID_H264:
			srcFormat = VS_H264;
			break;
		case AV_CODEC_ID_HEVC:
			srcFormat = VS_H265;
			break;
		case AV_CODEC_ID_MPEG2VIDEO:
			srcFormat = VS_MPEG2;
			break;
		}

		fmt_.format = srcFormat;
		fmt_.progressive = !(frame->interlaced_frame);
		fmt_.width = frame->width;
		fmt_.height = frame->height;
		fmt_.sarWidth = frame->sample_aspect_ratio.num;
		fmt_.sarHeight = frame->sample_aspect_ratio.den;
		fmt_.colorPrimaries = frame->color_primaries;
		fmt_.transferCharacteristics = frame->color_trc;
		fmt_.colorSpace = frame->colorspace;
		// ���̂Ƃ���Œ�t���[�����[�g�����Ή����Ȃ�
		fmt_.fixedFrameRate = true;
		fmt_.frameRateNum = stream->r_frame_rate.num;
		fmt_.frameRateDenom = stream->r_frame_rate.den;

		// x265�ŃC���^���[�X�̏ꍇ��field mode
		fieldMode_ = (fmt_.format == VS_H265 && fmt_.progressive == false);

		if (fieldMode_) {
			fmt_.height *= 2;
			fmt_.frameRateNum /= 2;
		}

		onVideoFormat(stream, fmt_);
	}

	// 2�̃t���[���̃g�b�v�t�B�[���h�A�{�g���t�B�[���h������
	static std::unique_ptr<av::Frame> mergeFields(av::Frame& topframe, av::Frame& bottomframe)
	{
		auto dstframe = std::unique_ptr<av::Frame>(new av::Frame());

		AVFrame* top = topframe();
		AVFrame* bottom = bottomframe();
		AVFrame* dst = (*dstframe)();

		// �t���[���̃v���p�e�B���R�s�[
		av_frame_copy_props(dst, top);

		// �������T�C�Y�Ɋւ�������R�s�[
		dst->format = top->format;
		dst->width = top->width;
		dst->height = top->height * 2;

		// �������m��
		if (av_frame_get_buffer(dst, 64) != 0) {
			THROW(RuntimeException, "failed to allocate frame buffer");
		}

		const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get((AVPixelFormat)(dst->format));
		int pixel_shift = (desc->comp[0].depth > 8) ? 1 : 0;
		int nplanes = (dst->format != AV_PIX_FMT_NV12) ? 3 : 2;

		for (int i = 0; i < nplanes; ++i) {
			int hshift = (i > 0 && dst->format != AV_PIX_FMT_NV12) ? desc->log2_chroma_w : 0;
			int vshift = (i > 0) ? desc->log2_chroma_h : 0;
			int wbytes = (dst->width >> hshift) << pixel_shift;
			int height = dst->height >> vshift;

			for (int y = 0; y < height; y += 2) {
				uint8_t* dst0 = dst->data[i] + dst->linesize[i] * (y + 0);
				uint8_t* dst1 = dst->data[i] + dst->linesize[i] * (y + 1);
				uint8_t* src0 = top->data[i] + top->linesize[i] * (y >> 1);
				uint8_t* src1 = bottom->data[i] + bottom->linesize[i] * (y >> 1);
				memcpy(dst0, src0, wbytes);
				memcpy(dst1, src1, wbytes);
			}
		}

		return std::move(dstframe);
	}
};

class VideoWriter : NonCopyable
{
public:
	VideoWriter(VideoFormat fmt, int bufsize)
		: ioCtx_(this, bufsize)
		, outputCtx_(ioCtx_, "yuv4mpegpipe")
		, codecCtx_(avcodec_find_encoder_by_name("wrapped_avframe"))
		, fmt_(fmt)
		, initialized_(false)
		, frameCount_(0)
	{ }

	void inputFrame(Frame& frame) {

		// �t�H�[�}�b�g�`�F�b�N
		ASSERT(fmt_.width == frame()->width);
		ASSERT(fmt_.height == frame()->height);
		ASSERT(fmt_.sarWidth == frame()->sample_aspect_ratio.num);
		ASSERT(fmt_.sarHeight == frame()->sample_aspect_ratio.den);
		ASSERT(fmt_.colorPrimaries == frame()->color_primaries);
		ASSERT(fmt_.transferCharacteristics == frame()->color_trc);
		ASSERT(fmt_.colorSpace == frame()->colorspace);

		// PTS�Ē�`
		frame()->pts = frameCount_++;

		init(frame);

		if (avcodec_send_frame(codecCtx_(), frame()) != 0) {
			THROW(FormatException, "avcodec_send_frame failed");
		}
		AVPacket packet = AVPacket();
		while (avcodec_receive_packet(codecCtx_(), &packet) == 0) {
			packet.stream_index = 0;
			av_interleaved_write_frame(outputCtx_(), &packet);
			av_packet_unref(&packet);
		}
	}

	void flush() {
		if (initialized_) {
			// flush encoder
			if (avcodec_send_frame(codecCtx_(), NULL) != 0) {
				THROW(FormatException, "avcodec_send_frame failed");
			}
			AVPacket packet = AVPacket();
			while (avcodec_receive_packet(codecCtx_(), &packet) == 0) {
				packet.stream_index = 0;
				av_interleaved_write_frame(outputCtx_(), &packet);
				av_packet_unref(&packet);
			}
			// flush muxer
			av_interleaved_write_frame(outputCtx_(), NULL);
		}
	}

	int getFrameCount() const {
		return frameCount_;
	}

	AVRational getAvgFrameRate() const {
		return av_make_q(fmt_.frameRateNum, fmt_.frameRateDenom);
	}

protected:
	virtual void onWrite(MemoryChunk mc) = 0;

private:
	class TransWriteContext : public WriteIOContext {
	public:
		TransWriteContext(VideoWriter* this_, int bufsize)
			: WriteIOContext(bufsize)
			, this_(this_)
		{ }
	protected:
		virtual void onWrite(MemoryChunk mc) {
			this_->onWrite(mc);
		}
	private:
		VideoWriter* this_;
	};

	TransWriteContext ioCtx_;
	OutputContext outputCtx_;
	CodecContext codecCtx_;
	VideoFormat fmt_;

	bool initialized_;
	int frameCount_;

	void init(Frame& frame)
	{
		if (initialized_ == false) {
			AVStream* st = avformat_new_stream(outputCtx_(), NULL);
			if (st == NULL) {
				THROW(FormatException, "avformat_new_stream failed");
			}

			AVCodecContext* enc = codecCtx_();

			enc->pix_fmt = (AVPixelFormat)frame()->format;
			enc->width = frame()->width;
			enc->height = frame()->height;
			enc->field_order = fmt_.progressive ? AV_FIELD_PROGRESSIVE : AV_FIELD_TT;
			enc->color_range = frame()->color_range;
			enc->color_primaries = frame()->color_primaries;
			enc->color_trc = frame()->color_trc;
			enc->colorspace = frame()->colorspace;
			enc->chroma_sample_location = frame()->chroma_location;
			st->sample_aspect_ratio = enc->sample_aspect_ratio = frame()->sample_aspect_ratio;

			st->time_base = enc->time_base = av_make_q(fmt_.frameRateDenom, fmt_.frameRateNum);
			st->avg_frame_rate = av_make_q(fmt_.frameRateNum, fmt_.frameRateDenom);

			if (avcodec_open2(codecCtx_(), codecCtx_()->codec, NULL) != 0) {
				THROW(FormatException, "avcodec_open2 failed");
			}

			// muxer�ɃG���R�[�_�p�����[�^��n��
			avcodec_parameters_from_context(st->codecpar, enc);

			// for debug
			av_dump_format(outputCtx_(), 0, "-", 1);

			if (avformat_write_header(outputCtx_(), NULL) < 0) {
				THROW(FormatException, "avformat_write_header failed");
			}
			initialized_ = true;
		}
	}
};

class AudioWriter : NonCopyable
{
public:
	AudioWriter(AVStream* src, int bufsize)
		: ioCtx_(this, bufsize)
		, outputCtx_(ioCtx_, "adts")
		, frameCount_(0)
	{
		AVStream* st = avformat_new_stream(outputCtx_(), NULL);
		if (st == NULL) {
			THROW(FormatException, "avformat_new_stream failed");
		}

		// �R�[�f�b�N�p�����[�^���R�s�[
		avcodec_parameters_copy(st->codecpar, src->codecpar);

		// for debug
		av_dump_format(outputCtx_(), 0, "-", 1);

		if (avformat_write_header(outputCtx_(), NULL) < 0) {
			THROW(FormatException, "avformat_write_header failed");
		}
	}

	void inputFrame(AVPacket& frame) {
		// av_interleaved_write_frame��packet��ownership��n���̂�
		AVPacket outpacket = AVPacket();
		av_packet_ref(&outpacket, &frame);
		outpacket.stream_index = 0;
		outpacket.pos = -1;
		if (av_interleaved_write_frame(outputCtx_(), &outpacket) < 0) {
			THROW(FormatException, "av_interleaved_write_frame failed");
		}
	}

	void flush() {
		// flush muxer
		if (av_interleaved_write_frame(outputCtx_(), NULL) < 0) {
			THROW(FormatException, "av_interleaved_write_frame failed");
		}
	}

protected:
	virtual void onWrite(MemoryChunk mc) = 0;

private:
	class TransWriteContext : public WriteIOContext {
	public:
		TransWriteContext(AudioWriter* this_, int bufsize)
			: WriteIOContext(bufsize)
			, this_(this_)
		{ }
	protected:
		virtual void onWrite(MemoryChunk mc) {
			this_->onWrite(mc);
		}
	private:
		AudioWriter* this_;
	};

	TransWriteContext ioCtx_;
	OutputContext outputCtx_;

	int frameCount_;
};

class Y4MParser
{
public:
	void clear() {
		y4mcur = 0;
		frameSize = 0;
		frameCount = 0;
		y4mbuf.clear();
	}
	void inputData(MemoryChunk mc) {
		y4mbuf.add(mc);
		while (true) {
			if (y4mcur == 0) {
				// �X�g���[���w�b�_
				auto data = y4mbuf.get();
				uint8_t* end = (uint8_t*)memchr(data.data, 0x0a, data.length);
				if (end == nullptr) {
					break;
				}
				*end = 0; // terminate
				char* next_token = nullptr;
				char* token = strtok_s((char*)data.data, " ", &next_token);
				// token == "YUV4MPEG2"
				int width = 0, height = 0, bp4p = 0;
				while (true) {
					token = strtok_s(nullptr, " ", &next_token);
					if (token == nullptr) {
						break;
					}
					switch (*(token++)) {
					case 'W':
						width = atoi(token);
						break;
					case 'H':
						height = atoi(token);
						break;
					case 'C':
						if (strcmp(token, "420jpeg") == 0 ||
							strcmp(token, "420mpeg2") == 0 ||
							strcmp(token, "420paldv") == 0) {
							bp4p = 6;
						}
						else if (strcmp(token, "mono") == 0) {
							bp4p = 4;
						}
						else if (strcmp(token, "mono16") == 0) {
							bp4p = 8;
						}
						else if (strcmp(token, "411") == 0) {
							bp4p = 6;
						}
						else if (strcmp(token, "422") == 0) {
							bp4p = 8;
						}
						else if (strcmp(token, "444") == 0) {
							bp4p = 12;
						}
						else if (strcmp(token, "420p9") == 0 ||
							strcmp(token, "420p10") == 0 ||
							strcmp(token, "420p12") == 0 ||
							strcmp(token, "420p14") == 0 ||
							strcmp(token, "420p16") == 0) {
							bp4p = 12;
						}
						else if (strcmp(token, "422p9") == 0 ||
							strcmp(token, "422p10") == 0 ||
							strcmp(token, "422p12") == 0 ||
							strcmp(token, "422p14") == 0 ||
							strcmp(token, "422p16") == 0) {
							bp4p = 16;
						}
						else if (strcmp(token, "444p9") == 0 ||
							strcmp(token, "444p10") == 0 ||
							strcmp(token, "444p12") == 0 ||
							strcmp(token, "444p14") == 0 ||
							strcmp(token, "444p16") == 0) {
							bp4p = 24;
						}
						else {
							THROWF(FormatException, "[y4m] Unknown pixel format: %s", token);
						}
						break;
					}
				}
				if (width == 0 || height == 0 || bp4p == 0) {
					THROW(FormatException, "[y4m] missing stream information");
				}
				frameSize = (width * height * bp4p) >> 2;
				y4mbuf.trimHead(end - data.data + 1);
				y4mcur = 1; // ���̓t���[���w�b�_
			}
			if (y4mcur == 1) {
				// �t���[���w�b�_
				auto data = y4mbuf.get();
				uint8_t* end = (uint8_t*)memchr(data.data, 0x0a, data.length);
				if (end == nullptr) {
					break;
				}
				y4mbuf.trimHead(end - data.data + 1);
				y4mcur = 2; // ���̓t���[���f�[�^
			}
			if (y4mcur == 2) {
				// �t���[���f�[�^
				if (y4mbuf.size() < frameSize) {
					break;
				}
				y4mbuf.trimHead(frameSize);
				frameCount++;
				y4mcur = 1; // ���̓t���[���w�b�_
			}
		}
	}
	int getFrameCount() const {
		return frameCount;
	}
private:
	int y4mcur;
	int frameSize;
	int frameCount;
	AutoBuffer y4mbuf;
};

class EncodeWriter : AMTObject, NonCopyable
{
public:
	EncodeWriter(AMTContext& ctx)
		: AMTObject(ctx)
		, videoWriter_(NULL)
		, process_(NULL)
		, error_(false)
	{ }
	~EncodeWriter()
	{
		if (process_ != NULL && process_->isRunning()) {
			THROW(InvalidOperationException, "call finish before destroy object ...");
		}

		delete videoWriter_;
		delete process_;
	}

	void start(const tstring& encoder_args, VideoFormat fmt, bool fieldMode, int bufsize) {
		if (videoWriter_ != NULL) {
			THROW(InvalidOperationException, "start method called multiple times");
		}
		fieldMode_ = fieldMode;
		if (fieldMode) {
			// �t�B�[���h���[�h�̂Ƃ��͉𑜓x�͏c1/2��FPS��2�{
			fmt.height /= 2;
			fmt.frameRateNum *= 2;
		}
		y4mparser.clear();
		videoWriter_ = new MyVideoWriter(this, fmt, bufsize);
		process_ = new StdRedirectedSubProcess(encoder_args, 5);
	}

	void inputFrame(Frame& frame) {
		if (videoWriter_ == NULL) {
			THROW(InvalidOperationException, "you need to call start method before input frame");
		}
		if (error_) {
			THROW(RuntimeException, "failed to input frame due to encoder error ...");
		}
		if (fieldMode_) {
			// �t�B�[���h���[�h�̂Ƃ���top,bottom��2�ɕ����ďo��
			av::Frame top = av::Frame();
			av::Frame bottom = av::Frame();
			splitFrameToFields(frame, top, bottom);
			videoWriter_->inputFrame(top);
			videoWriter_->inputFrame(bottom);
		}
		else {
			videoWriter_->inputFrame(frame);
		}
	}

	void finish() {
		if (videoWriter_ != NULL) {
			videoWriter_->flush();
			process_->finishWrite();
			int ret = process_->join();
			if (ret != 0) {
				ctx.error("�������������G���R�[�_�Ō�̏o�́�����������");
				for (auto v : process_->getLastLines()) {
					v.push_back(0); // null terminate
					ctx.errorF("%s", v.data());
				}
				ctx.error("�������������G���R�[�_�Ō�̏o�́�����������");
				THROWF(RuntimeException, "�G���R�[�_�I���R�[�h: 0x%x", ret);
			}
			int inFrame = getFrameCount();
			int outFrame = y4mparser.getFrameCount();
			if (inFrame != outFrame) {
				THROWF(RuntimeException, "�t���[�����������܂���(%d vs %d)", inFrame, outFrame);
			}
		}
	}

	int getFrameCount() const {
		return videoWriter_->getFrameCount();
	}

	AVRational getFrameRate() const {
		return videoWriter_->getAvgFrameRate();
	}

	const std::deque<std::vector<char>>& getLastLines() {
		process_->getLastLines();
	}

private:
	class MyVideoWriter : public VideoWriter {
	public:
		MyVideoWriter(EncodeWriter* this_, VideoFormat fmt, int bufsize)
			: VideoWriter(fmt, bufsize)
			, this_(this_)
		{ }
	protected:
		virtual void onWrite(MemoryChunk mc) {
			this_->onVideoWrite(mc);
		}
	private:
		EncodeWriter* this_;
	};

	MyVideoWriter* videoWriter_;
	StdRedirectedSubProcess* process_;
	bool fieldMode_;
	bool error_;

	// �o�̓`�F�b�N�p�i�Ȃ��Ă������͖��Ȃ��j
	Y4MParser y4mparser;

	void onVideoWrite(MemoryChunk mc) {
		y4mparser.inputData(mc);
		try {
			process_->write(mc);
		}
		catch (Exception&) {
			error_ = true;
		}
	}

	VideoFormat getEncoderInputVideoFormat(VideoFormat format) {
		if (fieldMode_) {
			// �t�B�[���h���[�h�̂Ƃ��͉𑜓x�͏c1/2��FPS��2�{
			format.height /= 2;
			format.frameRateNum *= 2;
		}
		return format;
	}

	// 1�̃t���[�����g�b�v�t�B�[���h�A�{�g���t�B�[���h��2�̃t���[���ɕ���
	static void splitFrameToFields(av::Frame& frame, av::Frame& topfield, av::Frame& bottomfield)
	{
		AVFrame* src = frame();
		AVFrame* top = topfield();
		AVFrame* bottom = bottomfield();

		// �t���[���̃v���p�e�B���R�s�[
		av_frame_copy_props(top, src);
		av_frame_copy_props(bottom, src);

		// �������T�C�Y�Ɋւ�������R�s�[
		top->format = bottom->format = src->format;
		top->width = bottom->width = src->width;
		top->height = bottom->height = src->height / 2;

		// �������m��
		if (av_frame_get_buffer(top, 64) != 0) {
			THROW(RuntimeException, "failed to allocate frame buffer");
		}
		if (av_frame_get_buffer(bottom, 64) != 0) {
			THROW(RuntimeException, "failed to allocate frame buffer");
		}

		const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get((AVPixelFormat)(src->format));
		int pixel_shift = (desc->comp[0].depth > 8) ? 1 : 0;
		int nplanes = (src->format != AV_PIX_FMT_NV12) ? 3 : 2;

		for (int i = 0; i < nplanes; ++i) {
			int hshift = (i > 0 && src->format != AV_PIX_FMT_NV12) ? desc->log2_chroma_w : 0;
			int vshift = (i > 0) ? desc->log2_chroma_h : 0;
			int wbytes = (src->width >> hshift) << pixel_shift;
			int height = src->height >> vshift;

			for (int y = 0; y < height; y += 2) {
				uint8_t* src0 = src->data[i] + src->linesize[i] * (y + 0);
				uint8_t* src1 = src->data[i] + src->linesize[i] * (y + 1);
				uint8_t* dst0 = top->data[i] + top->linesize[i] * (y >> 1);
				uint8_t* dst1 = bottom->data[i] + bottom->linesize[i] * (y >> 1);
				memcpy(dst0, src0, wbytes);
				memcpy(dst1, src1, wbytes);
			}
		}
	}
};

} // namespace av

