#pragma once

/**
* Output stream construction
* Copyright (c) 2017-2019 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include <time.h>

#include <vector>
#include <map>
#include <memory>
#include <functional>

#include "CaptionData.h"
#include "StreamUtils.h"
#include "Mpeg2TsParser.h"

// ���Ԃ͑S�� 90kHz double �Ōv�Z����
// 90kHz�ł�60*1000/1001fps��1�t���[���̎��Ԃ͐����ŕ\���Ȃ�
// ������ƌ�����27MHz�ł͐��l���傫������

struct FileAudioFrameInfo : public AudioFrameInfo {
    int audioIdx;
    int codedDataSize;
    int waveDataSize;
    int64_t fileOffset;
    int64_t waveOffset;

    FileAudioFrameInfo();

    FileAudioFrameInfo(const AudioFrameInfo& info);
};

struct FileVideoFrameInfo : public VideoFrameInfo {
    int64_t fileOffset;

    FileVideoFrameInfo();

    FileVideoFrameInfo(const VideoFrameInfo& info);
};

enum StreamEventType {
    STREAM_EVENT_NONE = 0,
    PID_TABLE_CHANGED,
    VIDEO_FORMAT_CHANGED,
    AUDIO_FORMAT_CHANGED
};

struct StreamEvent {
    StreamEventType type;
    int frameIdx;	// �t���[���ԍ�
    int audioIdx;	// �ύX���ꂽ�����C���f�b�N�X�iAUDIO_FORMAT_CHANGED�̂Ƃ��̂ݗL���j
    int numAudio;	// �����̐��iPID_TABLE_CHANGED�̂Ƃ��̂ݗL���j
};

typedef std::vector<std::vector<int>> FileAudioFrameList;

struct OutVideoFormat {
    int formatId; // �����t�H�[�}�b�gID�i�ʂ��ԍ��j
    int videoFileId;
    VideoFormat videoFormat;
    std::vector<AudioFormat> audioFormat;
};

// ���Y�����v���
struct AudioDiffInfo {
    double sumPtsDiff;
    int totalSrcFrames;
    int totalAudioFrames; // �o�͂��������t���[���i�����������܂ށj
    int totalUniquAudioFrames; // �o�͂��������t���[���i�����������܂܂��j
    double maxPtsDiff;
    double maxPtsDiffPos;
    double basePts;

    // �b�P�ʂŎ擾
    double avgDiff() const;
    // �b�P�ʂŎ擾
    double maxDiff() const;

    void printAudioPtsDiff(AMTContext& ctx) const;

    void printToJson(StringBuilder& sb);

private:
    double elapsedTime(double modPTS) const;
};

struct FilterSourceFrame {
    bool halfDelay;
    int frameIndex; // �����p(DTS���t���[���ԍ�)
    double pts; // �����p
    double frameDuration; // �����p
    int64_t framePTS;
    int64_t fileOffset;
    int keyFrame;
    CMType cmType;
};

struct FilterAudioFrame {
    int frameIndex; // �f�o�b�O�p
    int64_t waveOffset;
    int waveLength;
};

struct FilterOutVideoInfo {
    int numFrames;
    int frameRateNum;
    int frameRateDenom;
    int fakeAudioSampleRate;
    std::vector<int> fakeAudioSamples;
};

struct OutCaptionLine {
    double start, end;
    CaptionLine* line;
};

typedef std::vector<std::vector<OutCaptionLine>> OutCaptionList;

struct NicoJKLine {
    double start, end;
    std::string line;

    void Write(const File& file) const;

    static NicoJKLine Read(const File& file);
};

typedef std::array<std::vector<NicoJKLine>, NICOJK_MAX> NicoJKList;

typedef std::pair<int64_t, JSTTime> TimeInfo;

struct EncodeFileInput {
    EncodeFileKey key;     // �L�[
    EncodeFileKey outKey; // �o�̓t�@�C�����p�L�[
    EncodeFileKey keyMax;  // �o�̓t�@�C��������p�ő�l
    double duration;       // �Đ�����
    std::vector<int> videoFrames; // �f���t���[�����X�g�i���g�̓t�B���^���̓t���[���ł̃C���f�b�N�X�j
    FileAudioFrameList audioFrames; // �����t���[�����X�g
    OutCaptionList captionList;     // ����
    NicoJKList nicojkList;          // �j�R�j�R�����R�����g
};

class StreamReformInfo : public AMTObject {
public:
    StreamReformInfo(
        AMTContext& ctx,
        int numVideoFile,
        std::vector<FileVideoFrameInfo>& videoFrameList,
        std::vector<FileAudioFrameInfo>& audioFrameList,
        std::vector<CaptionItem>& captionList,
        std::vector<StreamEvent>& streamEventList,
        std::vector<TimeInfo>& timeList);

    // 1. �R���X�g���N�g����ɌĂ�
    // splitSub: ���C���ȊO�̃t�H�[�}�b�g���������Ȃ�
    void prepare(bool splitSub, bool isEncodeAudio, bool isTsreplace);

    time_t getFirstFrameTime() const;

    // 2. �j�R�j�R�����R�����g���擾������Ă�
    void SetNicoJKList(const std::array<std::vector<NicoJKLine>, NICOJK_MAX>& nicoJKList);

    // 2. �e���ԉf���t�@�C����CM��͌�ɌĂ�
    // cmzones: CM�]�[���i�t�B���^���̓t���[���ԍ��j
    // divs: �����|�C���g���X�g�i�t�B���^���̓t���[���ԍ��j
    void applyCMZones(int videoFileIndex, const std::vector<EncoderZone>& cmzones, const std::vector<int>& divs);

    // 3. CM��͂��I��������G���R�[�h�O�ɌĂ�
    // cmtypes: �o�͂���CM�^�C�v���X�g
    AudioDiffInfo genAudio(const std::vector<CMType>& cmtypes);

    // ���ԉf���t�@�C���̌�
    int getNumVideoFile() const;

    // ���͉f���K�i
    VIDEO_STREAM_FORMAT getVideoStreamFormat() const;

    // PMT�ύXPTS���X�g
    std::vector<int> getPidChangedList(int videoFileIndex) const;

    int getMainVideoFileIndex() const;

    // �t�B���^���͉f���t���[��
    const std::vector<FilterSourceFrame>& getFilterSourceFrames(int videoFileIndex) const;

    // �t�B���^���͉����t���[��
    const std::vector<FilterAudioFrame>& getFilterSourceAudioFrames(int videoFileIndex) const;

    // �o�̓t�@�C�����
    const EncodeFileInput& getEncodeFile(EncodeFileKey key) const;

    // ���Ԉꎞ�t�@�C�����Ƃ̏o�̓t�@�C����
    int getNumEncoders(int videoFileIndex) const;

    // ���v�o�̓t�@�C����
    //int getNumOutFiles() const {
    //	return (int)fileFormatId_.size();
    //}

    // video frame index -> VideoFrameInfo
    const VideoFrameInfo& getVideoFrameInfo(int frameIndex) const;

    // video frame index (DTS��) -> encoder index
    int getEncoderIndex(int frameIndex) const;

    // key��video,format��2�����g���Ȃ�
    const OutVideoFormat& getFormat(EncodeFileKey key) const;

    // genAudio��g�p�\
    const std::vector<EncodeFileKey>& getOutFileKeys() const;

    // �f���f�[�^�T�C�Y�i�o�C�g�j�A���ԁi�^�C���X�^���v�j�̃y�A
    std::pair<int64_t, double> getSrcVideoInfo(int videoFileIndex) const;

    // TODO: VFR�p�^�C���R�[�h�擾
    // infps: �t�B���^���͂�FPS
    // outpfs: �t�B���^�o�͂�FPS
    void getTimeCode(
        int encoderIndex, int videoFileIndex, CMType cmtype, double infps, double outfps) const;

    const std::vector<int64_t>& getAudioFileOffsets() const;

    bool isVFR() const;

    bool hasRFF() const;

    double getInDuration() const;

    std::pair<double, double> getInOutDuration() const;

    // �����t���[���ԍ����X�g����FilterAudioFrame���X�g�ɕϊ�
    std::vector<FilterAudioFrame> getWaveInput(const std::vector<int>& frameList) const;

    void printOutputMapping(std::function<tstring(EncodeFileKey)> getFileName) const;

    // �ȉ��f�o�b�O�p //

    void serialize(const tstring& path);

    void serialize(const File& file);

    static StreamReformInfo deserialize(AMTContext& ctx, const tstring& path);

    static StreamReformInfo deserialize(AMTContext& ctx, const File& file);

private:

    struct CaptionDuration {
        double startPTS, endPTS;
    };

    // ��v�C���f�b�N�X�̐���
    // DTS��: �S�f���t���[����DTS���ŕ��ׂ��Ƃ��̃C���f�b�N�X
    // PTS��: �S�f���t���[����PTS���ŕ��ׂ��Ƃ��̃C���f�b�N�X
    // ���ԉf���t�@�C����: ���ԉf���t�@�C���̃C���f�b�N�X(=video)
    // �t�H�[�}�b�g��: �S�t�H�[�}�b�g�̃C���f�b�N�X
    // �t�H�[�}�b�g(�o��)��: ��{�I�Ƀt�H�[�}�b�g�Ɠ��������A�u���C���ȊO�͌������Ȃ��v�ꍇ�A
    //                     ���C���ȊO����������ĈقȂ�C���f�b�N�X�ɂȂ��Ă���(=format)
    // �o�̓t�@�C����: EncodeFileKey�Ŏ��ʂ����o�̓t�@�C���̃C���f�b�N�X

    // ���͉�͂̏o��
    int numVideoFile_;
    std::vector<FileVideoFrameInfo> videoFrameList_; // [DTS��] 
    std::vector<FileAudioFrameInfo> audioFrameList_;
    std::vector<CaptionItem> captionItemList_;
    std::vector<StreamEvent> streamEventList_;
    std::vector<TimeInfo> timeList_;

    std::array<std::vector<NicoJKLine>, NICOJK_MAX> nicoJKList_;
    bool isEncodeAudio_;
    bool isTsreplace_;

    // �v�Z�f�[�^
    bool isVFR_;
    bool hasRFF_;
    std::vector<double> modifiedPTS_; // [DTS��] ���b�v�A���E���h���Ȃ�PTS
    std::vector<double> modifiedAudioPTS_; // ���b�v�A���E���h���Ȃ�PTS
    std::vector<double> modifiedCaptionPTS_; // ���b�v�A���E���h���Ȃ�PTS
    std::vector<double> audioFrameDuration_; // �e�����t���[���̎���
    std::vector<int> ordredVideoFrame_; // [PTS��] -> [DTS��] �ϊ�
    std::vector<double> dataPTS_; // [DTS��] �f���t���[���̃X�g���[����ł̈ʒu��PTS�̊֘A�t��
    std::vector<double> streamEventPTS_;
    std::vector<CaptionDuration> captionDuration_;

    std::vector<std::vector<int>> indexAudioFrameList_; // �����C���f�b�N�X���Ƃ̃t���[�����X�g

    std::vector<OutVideoFormat> format_; // [�t�H�[�}�b�g��]
    // ���ԉf���t�@�C�����Ƃ̃t�H�[�}�b�g�J�n�C���f�b�N�X
    // �T�C�Y�͒��ԉf���t�@�C����+1
    std::vector<int> formatStartIndex_; // [���ԉf���t�@�C����]

    std::vector<int> fileFormatId_; // [�t�H�[�}�b�g(�o��)��] -> [�t�H�[�}�b�g��] �ϊ�
    // ���ԉf���t�@�C�����Ƃ̃t�@�C���J�n�C���f�b�N�X
    // �T�C�Y�͒��ԉf���t�@�C����+1
    std::vector<int> fileFormatStartIndex_; // [���ԉf���t�@�C����] -> [�t�H�[�}�b�g(�o��)��]

    // ���ԉf���t�@�C������
    std::vector<std::vector<FilterSourceFrame>> filterFrameList_; // [PTS��]
    std::vector<std::vector<FilterAudioFrame>> filterAudioFrameList_;
    std::vector<int64_t> filterSrcSize_;
    std::vector<double> filterSrcDuration_;
    std::vector<std::vector<int>> fileDivs_; // CM��͌���

    std::vector<int> frameFormatId_; // [DTS��] -> [�t�H�[�}�b�g(�o��)��]

    // �o�̓t�@�C�����X�g
    std::vector<EncodeFileKey> outFileKeys_; // [�o�̓t�@�C����]
    std::map<int, EncodeFileInput> outFiles_; // �L�[��EncodeFileKey.key()

    // �ŏ��̉f���t���[���̎���(UNIX����)
    time_t firstFrameTime_;

    std::vector<int64_t> audioFileOffsets_; // �����t�@�C���L���b�V���p

    double srcTotalDuration_;
    double outTotalDuration_;

    void reformMain(bool splitSub);

    void calcSizeAndTime(const std::vector<CMType>& cmtypes);

    template<typename I>
    void makeModifiedPTS(int64_t modifiedFirstPTS, std::vector<double>& modifiedPTS, const std::vector<I>& frames) {
        // �O��̃t���[����PTS��6���Ԉȏ�̂��ꂪ����Ɛ����������ł��Ȃ�
        if (frames.size() == 0) return;

        // ���b�v�A���E���h���Ȃ�PTS�𐶐�
        modifiedPTS.resize(frames.size());
        int64_t prevPTS = modifiedFirstPTS;
        for (int i = 0; i < int(frames.size()); ++i) {
            int64_t PTS = frames[i].PTS;
            if (PTS == -1) {
                // PTS���Ȃ�
                THROWF(FormatException,
                    "PTS������܂���B�����ł��܂���B %d�t���[����", i);
            }
            int64_t modPTS = prevPTS + int64_t((int32_t(PTS) - int32_t(prevPTS)));
            modifiedPTS[i] = (double)modPTS;
            prevPTS = modPTS;
        }

        // �X�g���[�����߂��Ă���ꍇ�͏����ł��Ȃ��̂ŃG���[�Ƃ���
        for (int i = 1; i < int(frames.size()); ++i) {
            if (modifiedPTS[i] - modifiedPTS[i - 1] < -60 * MPEG_CLOCK_HZ) {
                // 1���ȏ�߂��Ă���
                ctx.incrementCounter(AMT_ERR_NON_CONTINUOUS_PTS);
                ctx.warnF("PTS���߂��Ă��܂��B�����������ł��Ȃ���������܂���B [%d] %.0f -> %.0f",
                    i, modifiedPTS[i - 1], modifiedPTS[i]);
            }
        }
    }

    void registerOrGetFormat(OutVideoFormat& format);

    bool isEquealFormat(const OutVideoFormat& a, const OutVideoFormat& b);

    struct AudioState {
        double time = 0; // �ǉ����ꂽ�����t���[���̍��v����
        double lostPts = -1; // �����|�C���g����������PTS�i�\���p�j
        int lastFrame = -1;
    };

    struct OutFileState {
        int formatId; // �f�o�b�O�o�͗p
        double time; // �ǉ����ꂽ�f���t���[���̍��v����
        std::vector<AudioState> audioState;
        FileAudioFrameList audioFrameList;
    };

    AudioDiffInfo initAudioDiffInfo();

    // �t�B���^���͂��特���\�z
    AudioDiffInfo genAudioStream();

    void genWaveAudioStream();

    // �\�[�X�t���[���̕\������
    // index, nextIndex: DTS��
    double getSourceFrameDuration(int index, int nextIndex);

    void addVideoFrame(OutFileState& file,
        const std::vector<AudioFormat>& audioFormat,
        double pts, double duration, AudioDiffInfo* adiff);

    void fillAudioFrames(
        OutFileState& file, int index, // �Ώۃt�@�C���Ɖ����C���f�b�N�X
        const AudioFormat* format, // �����t�H�[�}�b�g
        double pts, double duration, // �J�n�C��PTS��90kHz�ł̃^�C���X�p��
        AudioDiffInfo* adiff);

    // lastFrame���珇�ԂɌ��ĉ����t���[��������
    void fillAudioFramesInOrder(
        OutFileState& file, int index, // �Ώۃt�@�C���Ɖ����C���f�b�N�X
        const AudioFormat* format, // �����t�H�[�}�b�g
        double& pts, double& duration, // �J�n�C��PTS��90kHz�ł̃^�C���X�p��
        AudioDiffInfo* adiff);

    // �t�@�C���S�̂ł̎���
    std::pair<int, double> elapsedTime(double modPTS) const;

    void genCaptionStream();
};


