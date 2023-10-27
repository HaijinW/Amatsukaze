/**
* Amtasukaze Avisynth Source Plugin
* Copyright (c) 2017-2019 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/

#include "common.h"
#include "AudioEncoder.h"


void wave::set4(int8_t dst[4], const char* src) {
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];
} // namespace wave {

void EncodeAudio(AMTContext& ctx, const tstring& encoder_args,
    const tstring& audiopath, const AudioFormat& afmt,
    const std::vector<FilterAudioFrame>& audioFrames) {
    using namespace wave;

    ctx.info("[�����G���R�[�_�N��]");
    ctx.infoF("%s", encoder_args);

    auto process = std::unique_ptr<StdRedirectedSubProcess>(
        new StdRedirectedSubProcess(encoder_args, 5));

    int nchannels = 2;
    int bytesPerSample = 2;
    Header header;
    set4(header.chunkID, "RIFF");
    header.chunkSize = 0; // �T�C�Y����
    set4(header.format, "WAVE");
    set4(header.subchunk1ID, "fmt ");
    header.subchunk1Size = 16;
    header.audioFormat = 1;
    header.numChannels = nchannels;
    header.sampleRate = afmt.sampleRate;
    header.byteRate = afmt.sampleRate * bytesPerSample * nchannels;
    header.blockAlign = bytesPerSample * nchannels;
    header.bitsPerSample = bytesPerSample * 8;
    set4(header.subchunk2ID, "data");
    header.subchunk2Size = 0; // �T�C�Y����

    process->write(MemoryChunk((uint8_t*)&header, sizeof(header)));

    int audioSamplesPerFrame = 1024;
    // waveLength�̓[���̂��Ƃ�����̂Œ���
    for (int i = 0; i < (int)audioFrames.size(); ++i) {
        if (audioFrames[i].waveLength != 0) {
            audioSamplesPerFrame = audioFrames[i].waveLength / 4; // 16bit�X�e���I�O��
            break;
        }
    }

    File srcFile(audiopath, _T("rb"));
    AutoBuffer buffer;
    int frameWaveLength = audioSamplesPerFrame * bytesPerSample * nchannels;
    MemoryChunk mc = buffer.space(frameWaveLength);
    mc.length = frameWaveLength;

    for (size_t i = 0; i < audioFrames.size(); ++i) {
        if (audioFrames[i].waveLength != 0) {
            // wave������Ȃ�ǂ�
            srcFile.seek(audioFrames[i].waveOffset, SEEK_SET);
            srcFile.read(mc);
        } else {
            // �Ȃ��ꍇ�̓[�����߂���
            memset(mc.data, 0x00, mc.length);
        }
        process->write(mc);
    }

    process->finishWrite();
    int ret = process->join();
    if (ret != 0) {
        ctx.error("�����������������G���R�[�_�Ō�̏o�́�����������");
        for (auto v : process->getLastLines()) {
            v.push_back(0); // null terminate
            ctx.errorF("%s", v.data());
        }
        ctx.error("�����������������G���R�[�_�Ō�̏o�́�����������");
        THROWF(RuntimeException, "�����G���R�[�_�I���R�[�h: 0x%x", ret);
    }
}
