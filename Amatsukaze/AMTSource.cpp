/**
* Amtasukaze Avisynth Source Plugin
* Copyright (c) 2017-2019 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/

#include "AMTSource.h"


namespace av {


AVCodec* AMTSource::getHWAccelCodec(AVCodecID vcodecId) {
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

void AMTSource::MakeCodecContext(IScriptEnvironment* env) {
    AVCodecID vcodecId = videoStream->codecpar->codec_id;
    AVCodec *pCodec = getHWAccelCodec(vcodecId);
    if (pCodec == NULL) {
        ctx.warn("�w�肳�ꂽ�f�R�[�_���g�p�ł��Ȃ����߃f�t�H���g�f�R�[�_���g���܂�");
        pCodec = avcodec_find_decoder(vcodecId);
    }
    if (pCodec == NULL) {
        env->ThrowError("Could not find decoder ...");
    }
    codecCtx.Set(pCodec);
    if (avcodec_parameters_to_context(codecCtx(), videoStream->codecpar) != 0) {
        env->ThrowError("avcodec_parameters_to_context failed");
    }
    codecCtx()->pkt_timebase = videoStream->time_base;
    codecCtx()->thread_count = GetFFmpegThreads((decodeThreads) ? decodeThreads : GetProcessorCount());

    // export_mvs for codecview
    //AVDictionary *opts = NULL;
    //av_dict_set(&opts, "flags2", "+export_mvs", 0);

    if (avcodec_open2(codecCtx(), pCodec, NULL) != 0) {
        env->ThrowError("avcodec_open2 failed");
    }
}

#if ENABLE_FFMPEG_FILTER
void AMTSource::MakeFilterGraph(IScriptEnvironment* env) {
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

void AMTSource::MakeVideoInfo(const VideoFormat& vfmt, const AudioFormat& afmt) {
    vi.width = vfmt.width;
    vi.height = vfmt.height;
    vi.SetFPS(vfmt.frameRateNum, vfmt.frameRateDenom);
    vi.num_frames = int(frames.size());

    interlaced = !vfmt.progressive;

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
    } else {
        // No audio
        vi.audio_samples_per_second = 0;
        vi.num_audio_samples = 0;
        vi.nchannels = 0;
    }
}

void AMTSource::UpdateVideoInfo(IScriptEnvironment* env) {
    // �r�b�g�[�x�͎擾���ĂȂ��̂�ffmpeg����擾����
    vi.pixel_type = toAVSFormat(codecCtx()->pix_fmt, env);

#if ENABLE_FFMPEG_FILTER
    if (bufferSinkCtx) {
        // �t�B���^������΃t�B���^�̏o�͂ɍX�V
        const AVFilterLink* outlink = bufferSinkCtx->inputs[0];
        vi.pixel_type = toAVSFormat((AVPixelFormat)outlink->format, env);

        if (outlink->w != vi.width ||
            outlink->h != vi.height) {
            env->ThrowError("ffmpeg filter output is resized, which is not supported on current AMTSource.");
        }
    }
#endif
}

void AMTSource::ResetDecoder(IScriptEnvironment* env) {
    lastDecodeFrame = -1;
    prevFrame = nullptr;
    MakeCodecContext(env);
#if ENABLE_FFMPEG_FILTER
    if (filterdesc.size()) {
        MakeFilterGraph(env);
    }
#endif
}

PVideoFrame AMTSource::MakeFrame(AVFrame* top, AVFrame* bottom, IScriptEnvironment* env) {
    PVideoFrame ret = env->NewVideoFrame(vi);
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get((AVPixelFormat)(top->format));

    if (desc->comp[0].depth > 8) {
        MergeField<uint16_t>(ret, top, bottom);
    } else {
        MergeField<uint8_t>(ret, top, bottom);
    }

    // �t���[���^�C�v
    ret->SetProperty("FrameType", top->pict_type);

    // QP�e�[�u��
    if (outputQP) {
        int qp_stride, qp_scale_type;
        const int8_t* qp_table = av_frame_get_qp_table(top, &qp_stride, &qp_scale_type);
        if (qp_table) {
            VideoInfo qpvi = vi;
            if (!qp_stride) {
                qpvi.width = (vi.width + 15) >> 4;
                qpvi.height = 1;
            } else {
                qpvi.width = qp_stride;
                qpvi.height = (vi.height + 15) >> 4;
            }
            qpvi.pixel_type = VideoInfo::CS_Y8;
            PVideoFrame qpframe = env->NewVideoFrame(qpvi);
            env->BitBlt(qpframe->GetWritePtr(), qpframe->GetPitch(),
                (const BYTE*)qp_table, qpvi.width, qpvi.width, qpvi.height);
            if (top->pict_type != AV_PICTURE_TYPE_B) {
                nonBQPTable = qpframe;
            }
            ret->SetProperty("QP_Table", qpframe);
            ret->SetProperty("QP_Table_Non_B", nonBQPTable);
            ret->SetProperty("QP_Stride", qp_stride ? qpframe->GetPitch() : 0);
            ret->SetProperty("QP_ScaleType", qp_scale_type);

            PVideoFrame dcframe = env->NewVideoFrame(qpvi);
            auto dc_table_data = av_frame_get_side_data(top, AV_FRAME_DATA_MB_DC_TABLE_DATA);
            if (dc_table_data) {
                env->BitBlt(dcframe->GetWritePtr(), dcframe->GetPitch(),
                    (const BYTE*)dc_table_data->data, qpvi.width, qpvi.width, qpvi.height);
                ret->SetProperty("DC_Table", dcframe);
            }
        }
    }

    return ret;
}

void AMTSource::PutFrame(int n, const PVideoFrame& frame) {
    CacheFrame* pcache = new CacheFrame();
    pcache->data = frame;
    pcache->key = n;
    frameCache[n] = pcache;
    recentAccessed.push_front(pcache);

    if ((int)recentAccessed.size() > seekDistance * 3 / 2) {
        // �L���b�V�������ꂽ��폜
        CacheFrame* pdel = recentAccessed.back();
        frameCache.erase(pdel->key);
        recentAccessed.pop_back();
        delete pdel;
    }
}

int AMTSource::toAVSFormat(AVPixelFormat format, IScriptEnvironment* env) {
    // �r�b�g�[�x�擾
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(format);
    switch (desc->comp[0].depth) {
    case 8:
        return VideoInfo::CS_YV12;
    case 10:
        return VideoInfo::CS_YUV420P10;
    case 12:
        return VideoInfo::CS_YUV420P12;
    }
    env->ThrowError("�Ή����Ă��Ȃ��r�b�g�[�x�ł�");
    return 0;
}

#if ENABLE_FFMPEG_FILTER
void AMTSource::InputFrameFilter(Frame* frame, bool enableOut, IScriptEnvironment* env) {
    /* push the decoded frame into the filtergraph */
    if (av_buffersrc_add_frame_flags(bufferSrcCtx, frame ? (*frame)() : nullptr, 0) < 0) {
        env->ThrowError("av_buffersrc_add_frame_flags failed (Error while feeding the filtergraph)");
    }

    /* pull filtered frames from the filtergraph */
    while (1) {
        Frame filtered;
        int ret = av_buffersink_get_frame(bufferSinkCtx, filtered());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            // �����Ɠ��͂��K�v or �����t���[�����Ȃ�
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

void AMTSource::OnFrameDecoded(Frame& frame, IScriptEnvironment* env) {
    if (bufferSrcCtx) {
        // �t�B���^����
        //frame()->pts = frame()->best_effort_timestamp;
        InputFrameFilter(&frame, true, env);
    } else {
        OnFrameOutput(frame, env);
    }
}
#endif

void AMTSource::OnFrameOutput(Frame& frame, IScriptEnvironment* env) {
    // ffmpeg��pts wrap�̎d������Ȃ̂ŉ���33bit�݂̂�����
    //�i26���Ԉȏ゠�铮�悾�Əd������\���͂��邪�����j
    int64_t pts = frame()->pts & ((int64_t(1) << 33) - 1);

    int64_t headDiff = 0, tailDiff = 0;
    auto it = std::lower_bound(frames.begin(), frames.end(), pts, [](const FilterSourceFrame& e, int64_t pts) {
        return e.framePTS < pts;
        });

    if (it == frames.begin() && pts < it->framePTS) {
        headDiff = it->framePTS - pts;
        // �����������ꍇ��1�����ǉ����Č���
        pts += (int64_t(1) << 33);
        it = std::lower_bound(frames.begin(), frames.end(), pts, [](const FilterSourceFrame& e, int64_t pts) {
            return e.framePTS < pts;
            });
    }

    if (it == frames.end()) {
        // �Ō����낾����
        tailDiff = pts - frames.back().framePTS;
        // �O�̉\��������̂ŁA����
        if (headDiff == 0 || headDiff > tailDiff) {
            lastDecodeFrame = vi.num_frames;
        }
        prevFrame = nullptr; // �A���łȂ��Ȃ�ꍇ��null���Z�b�g
        return;
    }

    if (it->framePTS != pts) {
        // ��v����t���[�����Ȃ�
        ctx.incrementCounter(AMT_ERR_UNKNOWN_PTS);
        ctx.warnF("Unknown PTS frame %lld", pts);
        prevFrame = nullptr; // �A���łȂ��Ȃ�ꍇ��null���Z�b�g
        return;
    }

    int frameIndex = int(it - frames.begin());
    auto cacheit = frameCache.find(frameIndex);

    if (it->halfDelay) {
        // �f�B���C��K�p������
        if (cacheit != frameCache.end()) {
            // ���łɃL���b�V���ɂ���
            UpdateAccessed(cacheit->second);
            lastDecodeFrame = frameIndex;
        } else if (prevFrame != nullptr) {
            PutFrame(frameIndex, MakeFrame((*prevFrame)(), frame(), env));
            lastDecodeFrame = frameIndex;
        } else {
            // ���O�̃t���[�����Ȃ��̂Ńt���[�������Ȃ�
        }

        // ���̃t���[���������t���[�����Q�Ƃ��Ă��炻����o��
        auto next = it + 1;
        if (next != frames.end() && next->framePTS == it->framePTS) {
            auto cachenext = frameCache.find(frameIndex + 1);
            if (cachenext != frameCache.end()) {
                // ���łɃL���b�V���ɂ���
                UpdateAccessed(cachenext->second);
            } else {
                PutFrame(frameIndex + 1, MakeFrame(frame(), frame(), env));
            }
            lastDecodeFrame = frameIndex + 1;
        }
    } else {
        // ���̂܂�
        if (cacheit != frameCache.end()) {
            // ���łɃL���b�V���ɂ���
            UpdateAccessed(cacheit->second);
        } else {
            PutFrame(frameIndex, MakeFrame(frame(), frame(), env));
        }
        lastDecodeFrame = frameIndex;
    }

    prevFrame = std::unique_ptr<Frame>(new Frame(frame));
}

void AMTSource::UpdateAccessed(CacheFrame* frame) {
    // recentAccessed ��frame������ΐ擪�Ɉړ�
    auto target = std::find(recentAccessed.begin(), recentAccessed.end(), frame);
    if (target != recentAccessed.end()) {
        recentAccessed.erase(target);
    }
    recentAccessed.push_front(frame);
}

PVideoFrame AMTSource::ForceGetFrame(int n, IScriptEnvironment* env) {
    if (frameCache.size() == 0) {
        return env->NewVideoFrame(vi);
    }
    auto lb = frameCache.find(n);
    if (lb == frameCache.end()) {
        lb = frameCache.begin();
    }
    UpdateAccessed(lb->second);
    return lb->second->data;
}

void AMTSource::DecodeLoop(int goal, IScriptEnvironment* env) {
    Frame frame;
    AVPacket packet = AVPacket();

    // CUVID��pic_type���K�؂ɃZ�b�g����Ȃ��̂ŃL�[�t���[�����ǂ�����������Ȃ�
    // FFmpeg�͎Q�ƃt���[�������@���Ă��Ă��t���[����Ԃ��Ă��܂��̂�
    // �V�[�N��̍ŏ��̃t���[�����������܂މ\��������
    // �����FFmpeg�����̌��@�����Q�ƃt���[�����p�P�b�g���X�ɂ����̂Ȃ̂�
    // �V�[�N�ɂ����̂Ȃ̂��̋�ʂ��ł��Ȃ����߁A�d���Ȃ��̂����A
    // �V�[�N�ɂ����̂̏ꍇ�́A�{���̓f�R�[�h�ł����͂��̃t���[���Ȃ̂ŁA
    // ���̌������܂ރt���[����Ԃ��Ă��܂��͖̂��
    // pic_type���K�؂ɃZ�b�g�����f�R�[�_�̏ꍇ�́A
    // �uI�t���[�����炪�L���ȃt���[���v�Ɣ��肷��Ηǂ�������
    // pic_type���K�؂ɃZ�b�g����Ȃ��f�R�[�_�̏ꍇ�́A
    // ������I�t���[�����ǂ������肷��K�v������
    // packet����L�[�t���[����PTS���擾����
    // �f�R�[�h�����t���[��������PTS�Ȃ�L�[�t���[���Ɣ��f����
    int64_t keyFramePTS = -1;
    auto isFrameReady = [&]() {
        // �V�[�N��ŏ��̃t���[���łȂ��Ȃ�OK
        if (lastDecodeFrame != -1) return true;
        // �L�[�t���[���Ȃ�OK
        if (frame()->key_frame) return true;
        // �^�C���X�^���v���L�[�t���[���̂��̂Ȃ�L�[�t���[���Ɣ��f
        if (keyFramePTS != -1 && keyFramePTS == frame()->pts) return true;
        // �܂��L�[�t���[���łȂ��̂ŁA�������܂މ\��������
        return false;
        };

    while (av_read_frame(inputCtx(), &packet) == 0) {
        if (packet.stream_index == videoStream->index) {
            if ((packet.flags & AV_PKT_FLAG_KEY) && keyFramePTS == -1) {
                // �ŏ��̃L�[�t���[����PTS���o���Ă���
                keyFramePTS = packet.pts;
            }
            if (avcodec_send_packet(codecCtx(), &packet) != 0) {
                ctx.incrementCounter(AMT_ERR_DECODE_PACKET_FAILED);
                ctx.warn("avcodec_send_packet failed");
            }
            while (avcodec_receive_frame(codecCtx(), frame()) == 0) {
                // �ŏ��̓L�[�t���[���܂ŃX�L�b�v
                if (isFrameReady()) {
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
        // �X�g���[���͑S�ēǂݎ�����̂Ńt�B���^��flush
        InputFrameFilter(nullptr, true, env);
    }
#endif
}

void AMTSource::registerFailedFrames(int begin, int end, int replace, IScriptEnvironment* env) {
    for (int f = begin; f < end; ++f) {
        failedMap[f] = replace;
    }
    // �f�R�[�h�s�t���[�������P���𒴂���ꍇ�̓G���[�Ƃ���
    if (failedMap.size() * 10 > frames.size()) {
        env->ThrowError("[AMTSource] �f�R�[�h�ł��Ȃ��t���[�������������܂� -> %d�t���[�����f�R�[�h�s��",
            (int)failedMap.size());
    }
}

AMTSource::AMTSource(AMTContext& ctx,
    const tstring& srcpath,
    const tstring& audiopath,
    const VideoFormat& vfmt, const AudioFormat& afmt,
    const std::vector<FilterSourceFrame>& frames,
    const std::vector<FilterAudioFrame>& audioFrames,
    const DecoderSetting& decoderSetting,
    const int threads,
    const char* filterdesc,
    bool outputQP,
    IScriptEnvironment* env)
    : AMTObject(ctx)
    , frames(frames)
    , decoderSetting(decoderSetting)
    , decodeThreads(threads)
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
    , lastDecodeFrame(-1) {
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

    // ������
    ResetDecoder(env);
    UpdateVideoInfo(env);
}

AMTSource::~AMTSource() {
    // �L���b�V�����폜
    while (recentAccessed.size() > 0) {
        CacheFrame* pdel = recentAccessed.back();
        frameCache.erase(pdel->key);
        recentAccessed.pop_back();
        delete pdel;
    }
}

void AMTSource::TransferStreamInfo(std::unique_ptr<AMTSourceData>&& streamInfo) {
    storage = std::move(streamInfo);
}

PVideoFrame __stdcall AMTSource::GetFrame(int n, IScriptEnvironment* env) {
    std::lock_guard<std::mutex> guard(mutex);

    // �L���b�V���ɂ���ΕԂ�
    auto it = frameCache.find(n);
    if (it != frameCache.end()) {
        UpdateAccessed(it->second);
        return it->second->data;
    }

    // �f�R�[�h�ł��Ȃ��t���[���͒u���t���[���ɒu��������
    if (failedMap.find(n) != failedMap.end()) {
        n = failedMap[n];
    }

    // �L���b�V���ɂȂ��̂Ńf�R�[�h����
    if (lastDecodeFrame != -1 && n > lastDecodeFrame && n < lastDecodeFrame + seekDistance) {
        // �O�ɂ����߂�
        DecodeLoop(n, env);
    } else {
        // �V�[�N���ăf�R�[�h����
        int keyNum = frames[n].keyFrame;
        for (int i = 0; ; ++i) {
            int64_t fileOffset = frames[keyNum].fileOffset / 188 * 188;
            if (av_seek_frame(inputCtx(), -1, fileOffset, AVSEEK_FLAG_BYTE) < 0) {
                THROW(FormatException, "av_seek_frame failed");
            }
            ResetDecoder(env);
            DecodeLoop(n, env);
            if (frameCache.find(n) != frameCache.end()) {
                // �f�R�[�h����
                seekDistance = std::max(seekDistance, n - keyNum);
                break;
            }
            if (keyNum <= 0) {
                // ����ȏ�߂�Ȃ�
                // n����lastDecodeFrame�܂ł��f�R�[�h�s�Ƃ���
                registerFailedFrames(n, lastDecodeFrame, lastDecodeFrame, env);
                break;
            }
            if (lastDecodeFrame >= 0 && lastDecodeFrame < n) {
                // �f�[�^������Ȃ��ăS�[���ɓ��B�ł��Ȃ�����
                // ���̃t���[�������͑S�ăf�R�[�h�s�Ƃ���
                registerFailedFrames(lastDecodeFrame + 1, (int)frames.size(), lastDecodeFrame, env);
                break;
            }
            if (i == 2) {
                // �f�R�[�h���s
                // n����lastDecodeFrame�܂ł��f�R�[�h�s�Ƃ���
                registerFailedFrames(n, lastDecodeFrame, lastDecodeFrame, env);
                break;
            }
            keyNum -= std::max(5, keyNum - frames[keyNum - 1].keyFrame);
        }
    }

    return ForceGetFrame(n, env);
}

void __stdcall AMTSource::GetAudio(void* buf, __int64 start, __int64 count, IScriptEnvironment* env) {
    std::lock_guard<std::mutex> guard(mutex);

    if (audioFrames.size() == 0) return;

    const int sampleBytes = 4; // 16bit�X�e���I�O��
    int frameWaveLength = audioSamplesPerFrame * sampleBytes;
    uint8_t* ptr = (uint8_t*)buf;
    for (__int64 frameIndex = start / audioSamplesPerFrame, frameOffset = start % audioSamplesPerFrame;
        count > 0 && frameIndex < (__int64)audioFrames.size();
        ++frameIndex, frameOffset = 0) {
        // ���̃t���[���Ŗ��߂�ׂ��o�C�g��
        int readBytes = std::min<int>(
            (int)(frameWaveLength - frameOffset * sampleBytes),
            (int)count * sampleBytes);

        if (audioFrames[(size_t)frameIndex].waveLength != 0) {
            // wave������Ȃ�ǂ�
            waveFile.seek(audioFrames[(size_t)frameIndex].waveOffset + frameOffset * sampleBytes, SEEK_SET);
            waveFile.read(MemoryChunk(ptr, readBytes));
        } else {
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

const VideoInfo& __stdcall AMTSource::GetVideoInfo() { return vi; }

bool __stdcall AMTSource::GetParity(int n) {
    return interlaced;
}

int __stdcall AMTSource::SetCacheHints(int cachehints, int frame_range) {
    // ���ڃC���X�^���X�������ꍇ�AMTGuard������Ȃ��̂�MT_NICE_FILTER�ȊO�_��
    if (cachehints == CACHE_GET_MTMODE) return MT_NICE_FILTER;
    return 0;
};

AMTContext* g_ctx_for_plugin_filter = nullptr;

void SaveAMTSource(
    const tstring& savepath,
    const tstring& srcpath,
    const tstring& audiopath,
    const VideoFormat& vfmt, const AudioFormat& afmt,
    const std::vector<FilterSourceFrame>& frames,
    const std::vector<FilterAudioFrame>& audioFrames,
    const DecoderSetting& decoderSetting) {
    File file(savepath, _T("wb"));
    file.writeArray(std::vector<tchar>(srcpath.begin(), srcpath.end()));
    file.writeArray(std::vector<tchar>(audiopath.begin(), audiopath.end()));
    file.writeValue(vfmt);
    file.writeValue(afmt);
    file.writeArray(frames);
    file.writeArray(audioFrames);
    file.writeValue(decoderSetting);
}

PClip LoadAMTSource(const tstring& loadpath, const char* filterdesc, bool outputQP, int threads, IScriptEnvironment* env) {
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
        srcpath, audiopath, vfmt, afmt, data->frames, data->audioFrames, decoderSetting, threads, filterdesc, outputQP, env);
    src->TransferStreamInfo(std::move(data));
    return src;
}

AVSValue CreateAMTSource(AVSValue args, void* user_data, IScriptEnvironment* env) {
    if (g_ctx_for_plugin_filter == nullptr) {
        g_ctx_for_plugin_filter = new AMTContext();
    }
    tstring filename = to_tstring(args[0].AsString());
    const char* filterdesc = args[1].AsString("");
    const bool outputQP = args[2].AsBool(true);
    const int threads = args[3].AsInt(0);
    return LoadAMTSource(filename, filterdesc, outputQP, threads, env);
}

AVSLosslessSource::AVSLosslessSource(AMTContext& ctx, const tstring& filepath, const VideoFormat& format, IScriptEnvironment* env)
    : file(ctx, filepath, _T("rb"))
    , codec(make_unique_ptr(CCodec::CreateInstance(UTVF_ULH0, "Amatsukaze")))
    , vi() {
    file.readHeader();
    vi.width = file.getWidth();
    vi.height = file.getHeight();
    _ASSERT(format.width == vi.width);
    _ASSERT(format.height == vi.height);
    vi.num_frames = file.getNumFrames();
    vi.pixel_type = VideoInfo::CS_YV12;
    vi.SetFPS(format.frameRateNum, format.frameRateDenom);
    auto extra = file.getExtra();
    std::array<size_t, 3> cbGrossWidth = { CBGROSSWIDTH_WINDOWS, CBGROSSWIDTH_WINDOWS, CBGROSSWIDTH_WINDOWS };
    if (codec->DecodeBegin(UTVF_YV12, vi.width, vi.height, cbGrossWidth.data(), extra.data(), (int)extra.size())) {
        THROW(RuntimeException, "failed to DecodeBegin (UtVideo)");
    }

    size_t codedSize = codec->EncodeGetOutputSize(UTVF_YV12, vi.width, vi.height);
    codedFrame = std::unique_ptr<uint8_t[]>(new uint8_t[codedSize]);

    rawFrame = std::unique_ptr<uint8_t[]>(new uint8_t[vi.width * vi.height * 3 / 2]);
}

AVSLosslessSource::~AVSLosslessSource() {
    codec->DecodeEnd();
}

PVideoFrame __stdcall AVSLosslessSource::GetFrame(int n, IScriptEnvironment* env) {
    n = std::max(0, std::min(vi.num_frames - 1, n));
    file.readFrame(n, codedFrame.get());
    codec->DecodeFrame(rawFrame.get(), codedFrame.get());
    PVideoFrame dst = env->NewVideoFrame(vi);
    CopyYV12(dst, rawFrame.get(), vi.width, vi.height);
    return dst;
}

void __stdcall AVSLosslessSource::GetAudio(void* buf, __int64 start, __int64 count, IScriptEnvironment* env) { return; }
const VideoInfo& __stdcall AVSLosslessSource::GetVideoInfo() { return vi; }
bool __stdcall AVSLosslessSource::GetParity(int n) { return false; }

int __stdcall AVSLosslessSource::SetCacheHints(int cachehints, int frame_range) {
    if (cachehints == CACHE_GET_MTMODE) return MT_SERIALIZED;
    return 0;
};

} // namespace av {
