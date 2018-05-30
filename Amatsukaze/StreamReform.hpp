/**
* Output stream construction
* Copyright (c) 2017-2018 Nekopanda
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

#include "StreamUtils.hpp"

// ���Ԃ͑S�� 90kHz double �Ōv�Z����
// 90kHz�ł�60*1000/1001fps��1�t���[���̎��Ԃ͐����ŕ\���Ȃ�
// ������ƌ�����27MHz�ł͐��l���傫������

struct FileAudioFrameInfo : public AudioFrameInfo {
	int audioIdx;
	int codedDataSize;
	int waveDataSize;
	int64_t fileOffset;
	int64_t waveOffset;

	FileAudioFrameInfo()
		: AudioFrameInfo()
		, audioIdx(0)
		, codedDataSize(0)
		, waveDataSize(0)
		, fileOffset(0)
		, waveOffset(-1)
	{ }

	FileAudioFrameInfo(const AudioFrameInfo& info)
		: AudioFrameInfo(info)
		, audioIdx(0)
		, codedDataSize(0)
		, waveDataSize(0)
		, fileOffset(0)
		, waveOffset(-1)
	{ }
};

struct FileVideoFrameInfo : public VideoFrameInfo {
	int64_t fileOffset;

	FileVideoFrameInfo()
		: VideoFrameInfo()
		, fileOffset(0)
	{ }

	FileVideoFrameInfo(const VideoFrameInfo& info)
		: VideoFrameInfo(info)
		, fileOffset(0)
	{ }
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
	double avgDiff() const {
		return ((double)sumPtsDiff / totalAudioFrames) / MPEG_CLOCK_HZ;
	}
	// �b�P�ʂŎ擾
	double maxDiff() const {
		return (double)maxPtsDiff / MPEG_CLOCK_HZ;
	}

	void printAudioPtsDiff(AMTContext& ctx) const {
		double avgDiff = this->avgDiff() * 1000;
		double maxDiff = this->maxDiff() * 1000;
		int notIncluded = totalSrcFrames - totalUniquAudioFrames;

		ctx.info("�o�͉����t���[��: %d�i�����������t���[��%d�j",
			totalAudioFrames, totalAudioFrames - totalUniquAudioFrames);
		ctx.info("���o�̓t���[��: %d�i%.3f%%�j",
			notIncluded, (double)notIncluded * 100 / totalSrcFrames);

		ctx.info("���Y��: ���� %.2fms �ő� %.2fms",
			avgDiff, maxDiff);
		if (maxPtsDiff > 0 && maxDiff - avgDiff > 1) {
			double sec = elapsedTime(maxPtsDiffPos);
			int minutes = (int)(sec / 60);
			sec -= minutes * 60;
			ctx.info("�ő剹�Y���ʒu: ���͍ŏ��̉f���t���[������%d��%.3f�b��",
				minutes, sec);
		}
	}

	void printToJson(StringBuilder& sb) {
		double avgDiff = this->avgDiff() * 1000;
		double maxDiff = this->maxDiff() * 1000;
		int notIncluded = totalSrcFrames - totalUniquAudioFrames;
		double maxDiffPos = maxPtsDiff > 0 ? elapsedTime(maxPtsDiffPos) : 0.0;

		sb.append(
			"{ \"totalsrcframes\": %d, \"totaloutframes\": %d, \"totaloutuniqueframes\": %d, "
			"\"notincludedper\": %g, \"avgdiff\": %g, \"maxdiff\": %g, \"maxdiffpos\": %g }",
			totalSrcFrames, totalAudioFrames, totalUniquAudioFrames, 
			(double)notIncluded * 100 / totalSrcFrames, avgDiff, maxDiff, maxDiffPos);
	}

private:
	double elapsedTime(double modPTS) const {
		return (double)(modPTS - basePts) / MPEG_CLOCK_HZ;
	}
};

struct FilterSourceFrame {
	bool halfDelay;
	int frameIndex; // �����p
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

	void Write(const File& file) const {
		file.writeValue(start);
		file.writeValue(end);
		file.writeString(line);
	}

	static NicoJKLine Read(const File& file) {
		NicoJKLine item;
		item.start = file.readValue<double>();
		item.end = file.readValue<double>();
		item.line = file.readString();
		return item;
	}
};

typedef std::array<std::vector<NicoJKLine>, NICOJK_MAX> NicoJKList;

typedef std::pair<int64_t, JSTTime> TimeInfo;

class StreamReformInfo : public AMTObject {
public:
	StreamReformInfo(
		AMTContext& ctx,
		int numVideoFile,
		std::vector<FileVideoFrameInfo>& videoFrameList,
		std::vector<FileAudioFrameInfo>& audioFrameList,
		std::vector<CaptionItem>& captionList,
		std::vector<StreamEvent>& streamEventList,
		std::vector<TimeInfo>& timeList)
		: AMTObject(ctx)
		, numVideoFile_(numVideoFile)
		, videoFrameList_(std::move(videoFrameList))
		, audioFrameList_(std::move(audioFrameList))
		, captionItemList_(std::move(captionList))
		, streamEventList_(std::move(streamEventList))
		, timeList_(std::move(timeList))
		, isVFR_(false)
		, hasRFF_(false)
		, srcTotalDuration_()
		, outTotalDuration_()
		, firstFrameTime_()
	{ }

	// 1.
	AudioDiffInfo prepare(bool splitSub) {
		reformMain(splitSub);
		return genWaveAudioStream();
	}

	// 2.
	void applyCMZones(int videoFileIndex, const std::vector<EncoderZone>& cmzones) {
		auto& frames = filterFrameList_[videoFileIndex];
		for (auto zone : cmzones) {
			for (int i = zone.startFrame; i < zone.endFrame; ++i) {
				frames[i].cmType = CMTYPE_CM;
			}
		}
	}

	time_t getFirstFrameTime() const {
		return firstFrameTime_;
	}

	void SetNicoJKList(const std::array<std::vector<NicoJKLine>, NICOJK_MAX>& nicoJKList) {
		for (int t = 0; t < NICOJK_MAX; ++t) {
			nicoJKList_[t].resize(nicoJKList[t].size());
			double startTime = dataPTS_.front();
			for (int i = 0; i < (int)nicoJKList[t].size(); ++i) {
				auto& src = nicoJKList[t][i];
				auto& dst = nicoJKList_[t][i];
				// �J�n�f���I�t�Z�b�g�����Z
				dst.start = src.start + startTime;
				dst.end = src.end + startTime;
				dst.line = src.line;
			}
		}
	}

	// 3.
	AudioDiffInfo genAudio() {
		calcSizeAndTime();
		genCaptionStream();
		return genAudioStream();
	}

	//AudioDiffInfo prepareEncode() {
	//	reformMain();
	//	genAudioStream();
	//	return genWaveAudioStream();
	//}

	// ���ԉf���t�@�C���̌�
	int getNumVideoFile() const {
		return numVideoFile_;
	}

	// ���͉f���K�i
	VIDEO_STREAM_FORMAT getVideoStreamFormat() const {
		return videoFrameList_[0].format.format;
	}

	// �t�B���^���͉f���t���[��
	const std::vector<FilterSourceFrame>& getFilterSourceFrames(int videoFileIndex) const {
		return filterFrameList_[videoFileIndex];
	}

	// �t�B���^���͉����t���[��
	const std::vector<FilterAudioFrame>& getFilterSourceAudioFrames(int videoFileIndex) const {
		return filterAudioFrameList_[videoFileIndex];
	}

	// ���Ԉꎞ�t�@�C�����Ƃ̏o�̓t�@�C����
	int getNumEncoders(int videoFileIndex) const {
		return int(
			videoFileStartIndex_[videoFileIndex + 1] - videoFileStartIndex_[videoFileIndex]);
	}

	// ���v�o�̓t�@�C����
	int getNumOutFiles() const {
		return (int)fileFormatId_.size();
	}

	// video frame index -> VideoFrameInfo
	const VideoFrameInfo& getVideoFrameInfo(int frameIndex) const {
		return videoFrameList_[frameIndex];
	}

	// video frame index -> encoder index
	int getEncoderIndex(int frameIndex) const {
		int fileId = frameFileId_[frameIndex];
		const auto& format = outFormat_[fileFormatId_[fileId]];
		return fileId - videoFormatStartIndex_[format.videoFileId];
	}

	const OutVideoFormat& getFormat(int encoderIndex, int videoFileIndex) const {
		int fileId = videoFileStartIndex_[videoFileIndex] + encoderIndex;
		return outFormat_[fileFormatId_[fileId]];
	}

	// �o�͒ʂ��ԍ�
	int getOutFileIndex(int encoderIndex, int videoFileIndex) const {
		return videoFileStartIndex_[videoFileIndex] + encoderIndex;
	}

	// �f���f�[�^�T�C�Y�i�o�C�g�j�A���ԁi�^�C���X�^���v�j�̃y�A
	std::pair<int64_t, double> getSrcVideoInfo(int encoderIndex, int videoFileIndex) const {
		int fileId = videoFileStartIndex_[videoFileIndex] + encoderIndex;
		return std::make_pair(fileSrcSize_[fileId], fileSrcDuration_[fileId]);
	}

	const FileAudioFrameList& getFileAudioFrameList(
		int encoderIndex, int videoFileIndex, CMType cmtype) const
	{
		int fileId = videoFileStartIndex_[videoFileIndex] + encoderIndex;
		return *reformedAudioFrameList_[fileId][cmtype].get();
	}

	const OutCaptionList& getOutCaptionList(
		int encoderIndex, int videoFileIndex, CMType cmtype) const
	{
		int fileId = videoFileStartIndex_[videoFileIndex] + encoderIndex;
		return *reformedCationList_[fileId][cmtype].get();
	}

	const NicoJKList& getOutNicoJKList(
		int encoderIndex, int videoFileIndex, CMType cmtype) const
	{
		int fileId = videoFileStartIndex_[videoFileIndex] + encoderIndex;
		return *reformedNicoJKList_[fileId][cmtype].get();
	}

	// TODO: VFR�p�^�C���R�[�h�擾
	// infps: �t�B���^���͂�FPS
	// outpfs: �t�B���^�o�͂�FPS
	void getTimeCode(
		int encoderIndex, int videoFileIndex, CMType cmtype, double infps, double outfps) const
	{
		//
	}

	// �e�t�@�C���̍Đ�����
	double getFileDuration(int encoderIndex, int videoFileIndex, CMType cmtype) const {
		int fileId = videoFileStartIndex_[videoFileIndex] + encoderIndex;
		return fileDuration_[fileId][cmtype];
	}

	const std::vector<int64_t>& getAudioFileOffsets() const {
		return audioFileOffsets_;
	}

	const std::vector<int>& getOutFileMapping() const {
		return outFileIndex_;
	}

	bool isVFR() const {
		return isVFR_;
	}

	bool hasRFF() const {
		return hasRFF_;
	}

	double getInDuration() const {
		return srcTotalDuration_;
	}

	std::pair<double, double> getInOutDuration() const {
		return std::make_pair(srcTotalDuration_, outTotalDuration_);
	}

	void printOutputMapping(std::function<std::string(int)> getFileName) const
	{
		ctx.info("[�o�̓t�@�C��]");
		for (int i = 0; i < (int)fileFormatId_.size(); ++i) {
			ctx.info("%d: %s", i, getFileName(i).c_str());
		}

		ctx.info("[����->�o�̓}�b�s���O]");
		double fromPTS = dataPTS_[0];
		int prevFileId = 0;
		for (int i = 0; i < (int)ordredVideoFrame_.size(); ++i) {
			int ordered = ordredVideoFrame_[i];
			double pts = modifiedPTS_[ordered];
			int fileId = frameFileId_[ordered];
			if (prevFileId != fileId) {
				// print
				auto from = elapsedTime(fromPTS);
				auto to = elapsedTime(pts);
				ctx.info("%3d��%05.3f�b - %3d��%05.3f�b -> %d",
					from.first, from.second, to.first, to.second, outFileIndex_[prevFileId]);
				prevFileId = fileId;
				fromPTS = pts;
			}
		}
		auto from = elapsedTime(fromPTS);
		auto to = elapsedTime(dataPTS_.back());
		ctx.info("%3d��%05.3f�b - %3d��%05.3f�b -> %d",
			from.first, from.second, to.first, to.second, outFileIndex_[prevFileId]);
	}

	// �ȉ��f�o�b�O�p //

	void serialize(const std::string& path) {
		serialize(File(path, "wb"));
	}

	void serialize(const File& file) {
		file.writeValue(numVideoFile_);
		file.writeArray(videoFrameList_);
		file.writeArray(audioFrameList_);
		WriteArray(file, captionItemList_);
		file.writeArray(streamEventList_);
		file.writeArray(timeList_);
	}

	static StreamReformInfo deserialize(AMTContext& ctx, const std::string& path) {
		return deserialize(ctx, File(path, "rb"));
	}

	static StreamReformInfo deserialize(AMTContext& ctx, const File& file) {
		int numVideoFile = file.readValue<int>();
		auto videoFrameList = file.readArray<FileVideoFrameInfo>();
		auto audioFrameList = file.readArray<FileAudioFrameInfo>();
		auto captionList = ReadArray<CaptionItem>(file);
		auto streamEventList = file.readArray<StreamEvent>();
		auto timeList = file.readArray<TimeInfo>();
		return StreamReformInfo(ctx,
			numVideoFile, videoFrameList, audioFrameList, captionList, streamEventList, timeList);
	}

private:

	struct CaptionDuration {
		double startPTS, endPTS;
	};

	// ���͉�͂̏o��
	int numVideoFile_;
	std::vector<FileVideoFrameInfo> videoFrameList_; // [DTS��] 
	std::vector<FileAudioFrameInfo> audioFrameList_;
	std::vector<CaptionItem> captionItemList_;
	std::vector<StreamEvent> streamEventList_;
	std::vector<TimeInfo> timeList_;

	std::array<std::vector<NicoJKLine>, NICOJK_MAX> nicoJKList_;

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

	std::vector<OutVideoFormat> outFormat_;
	// ���ԉf���t�@�C�����Ƃ̃t�H�[�}�b�g�J�n�C���f�b�N�X
	// �T�C�Y�͒��ԉf���t�@�C����+1
	std::vector<int> videoFormatStartIndex_;

	std::vector<int> fileFormatId_;
	// ���ԉf���t�@�C�����Ƃ̃t�@�C���J�n�C���f�b�N�X
	// �T�C�Y�͒��ԉf���t�@�C����+1
	std::vector<int> videoFileStartIndex_;

	// ���ԉf���t�@�C������
	std::vector<std::vector<FilterSourceFrame>> filterFrameList_; // [PTS��]
	std::vector<std::vector<FilterAudioFrame>> filterAudioFrameList_;

	std::vector<int> frameFileId_; // videoFrameList_�Ɠ����T�C�Y
	//std::map<int64_t, int> framePtsMap_;

	// �o�̓t�@�C�����Ƃ̓��͉f���f�[�^�T�C�Y�A����
	std::vector<int64_t> fileSrcSize_;
	std::vector<double> fileSrcDuration_;
	std::vector<std::array<double, CMTYPE_MAX>> fileDuration_; // �e�t�@�C���̍Đ�����

	// �ŏ��̉f���t���[���̎���(UNIX����)
	time_t firstFrameTime_;

	// 2nd phase �o��
	//std::vector<bool> encodedFrames_;

	// �����\�z�p
	std::vector<std::array<std::unique_ptr<FileAudioFrameList>, CMTYPE_MAX>> reformedAudioFrameList_;

	std::vector<int64_t> audioFileOffsets_; // �����t�@�C���L���b�V���p
	std::vector<int> outFileIndex_; // �o�͔ԍ��}�b�s���O

	double srcTotalDuration_;
	double outTotalDuration_;

	// �����\�z�p
	std::vector<std::array<std::unique_ptr<OutCaptionList>, CMTYPE_MAX>> reformedCationList_;
	std::vector<std::array<std::unique_ptr<NicoJKList>, CMTYPE_MAX>> reformedNicoJKList_;

	void reformMain(bool splitSub)
	{
		if (videoFrameList_.size() == 0) {
			THROW(FormatException, "�f���t���[����1��������܂���");
		}
		if (audioFrameList_.size() == 0) {
			THROW(FormatException, "�����t���[����1��������܂���");
		}
		if (streamEventList_.size() == 0 || streamEventList_[0].type != PID_TABLE_CHANGED) {
			THROW(FormatException, "�s���ȃf�[�^�ł�");
		}

		/*
		// framePtsMap_���쐬�i�����ɍ���̂Łj
		for (int i = 0; i < int(videoFrameList_.size()); ++i) {
			framePtsMap_[videoFrameList_[i].PTS] = i;
		}
		*/

		// VFR���o
		isVFR_ = false;
		for (int i = 0; i < int(videoFrameList_.size()); ++i) {
			if (videoFrameList_[i].format.fixedFrameRate == false) {
				isVFR_ = true;
				break;
			}
		}

		if (isVFR_) {
			THROW(FormatException, "���̃o�[�W������VFR�ɑΉ����Ă��܂���");
		}

		// �e�R���|�[�l���g�J�nPTS���f���t���[����̃��b�v�A���E���h���Ȃ�PTS�ɕϊ�
		//�i��������Ȃ��ƊJ�n�t���[�����m���ԂɃ��b�v�A���E���h������ł�Ɣ�r�ł��Ȃ��Ȃ�j
		std::vector<int64_t> startPTSs;
		startPTSs.push_back(videoFrameList_[0].PTS);
		startPTSs.push_back(audioFrameList_[0].PTS);
		if (captionItemList_.size() > 0) {
			startPTSs.push_back(captionItemList_[0].PTS);
		}
		int64_t modifiedStartPTS[3];
		int64_t prevPTS = startPTSs[0];
		for (int i = 0; i < int(startPTSs.size()); ++i) {
			int64_t PTS = startPTSs[i];
			int64_t modPTS = prevPTS + int64_t((int32_t(PTS) - int32_t(prevPTS)));
			modifiedStartPTS[i] = modPTS;
			prevPTS = modPTS;
		}

		// �e�R���|�[�l���g�̃��b�v�A���E���h���Ȃ�PTS�𐶐�
		makeModifiedPTS(modifiedStartPTS[0], modifiedPTS_, videoFrameList_);
		makeModifiedPTS(modifiedStartPTS[1], modifiedAudioPTS_, audioFrameList_);
		makeModifiedPTS(modifiedStartPTS[2], modifiedCaptionPTS_, captionItemList_);

		// audioFrameDuration_�𐶐�
		audioFrameDuration_.resize(audioFrameList_.size());
		for (int i = 0; i < (int)audioFrameList_.size(); ++i) {
			const auto& frame = audioFrameList_[i];
			audioFrameDuration_[i] = (frame.numSamples * MPEG_CLOCK_HZ) / (double)frame.format.sampleRate;
		}

		// ptsOrdredVideoFrame_�𐶐�
		ordredVideoFrame_.resize(videoFrameList_.size());
		for (int i = 0; i < (int)videoFrameList_.size(); ++i) {
			ordredVideoFrame_[i] = i;
		}
		std::sort(ordredVideoFrame_.begin(), ordredVideoFrame_.end(), [&](int a, int b) {
			return modifiedPTS_[a] < modifiedPTS_[b];
		});

		// dataPTS�𐶐�
		// ��납�猩�Ă��̎��_�ōł�������PTS��dataPTS�Ƃ���
		double curMin = INFINITY;
		double curMax = 0;
		dataPTS_.resize(videoFrameList_.size());
		for (int i = (int)videoFrameList_.size() - 1; i >= 0; --i) {
			curMin = std::min(curMin, modifiedPTS_[i]);
			curMax = std::max(curMax, modifiedPTS_[i]);
			dataPTS_[i] = curMin;
		}

		// �����̊J�n�E�I�����v�Z
		captionDuration_.resize(captionItemList_.size());
		double curEnd = dataPTS_.back();
		for (int i = (int)captionItemList_.size() - 1; i >= 0; --i) {
			double modPTS = modifiedCaptionPTS_[i] + (captionItemList_[i].waitTime * (MPEG_CLOCK_HZ / 1000));
			if (captionItemList_[i].line) {
				captionDuration_[i].startPTS = modPTS;
				captionDuration_[i].endPTS = curEnd;
			}
			else {
				// �N���A
				captionDuration_[i].startPTS = captionDuration_[i].endPTS = modPTS;
				// �I�����X�V
				curEnd = modPTS;
			}
		}

		// �X�g���[���C�x���g��PTS���v�Z
		double endPTS = curMax + 1;
		streamEventPTS_.resize(streamEventList_.size());
		for (int i = 0; i < (int)streamEventList_.size(); ++i) {
			auto& ev = streamEventList_[i];
			double pts = -1;
			if (ev.type == PID_TABLE_CHANGED || ev.type == VIDEO_FORMAT_CHANGED) {
				if (ev.frameIdx >= (int)videoFrameList_.size()) {
					// ���߂��đΏۂ̃t���[�����Ȃ�
					pts = endPTS;
				}
				else {
					pts = dataPTS_[ev.frameIdx];
				}
			}
			else if (ev.type == AUDIO_FORMAT_CHANGED) {
				if (ev.frameIdx >= (int)audioFrameList_.size()) {
					// ���߂��đΏۂ̃t���[�����Ȃ�
					pts = endPTS;
				}
				else {
					pts = modifiedAudioPTS_[ev.frameIdx];
				}
			}
			streamEventPTS_[i] = pts;
		}

		// ���ԓI�ɋ߂��X�g���[���C�x���g��1�̕ω��_�Ƃ݂Ȃ�
		const double CHANGE_TORELANCE = 3 * MPEG_CLOCK_HZ;

		std::vector<int> sectionFormatList;
		std::vector<double> startPtsList;

		ctx.info("[�t�H�[�}�b�g�؂�ւ����]");

		// ���݂̉����t�H�[�}�b�g��ێ�
		// ����ES�����ω����Ă��O�̉����t�H�[�}�b�g�ƕς��Ȃ��ꍇ��
		// �C�x���g�����ł��Ȃ��̂ŁA���݂̉���ES���Ƃ͊֌W�Ȃ��S�����t�H�[�}�b�g��ێ�����
		std::vector<AudioFormat> curAudioFormats;

		OutVideoFormat curFormat = OutVideoFormat();
		double startPts = -1;
		double curFromPTS = -1;
		curFormat.videoFileId = -1;
		for (int i = 0; i < (int)streamEventList_.size(); ++i) {
			auto& ev = streamEventList_[i];
			double pts = streamEventPTS_[i];
			if (pts >= endPTS) {
				// ���ɉf�����Ȃ���ΈӖ����Ȃ�
				continue;
			}
			if (curFromPTS != -1 && curFromPTS + CHANGE_TORELANCE < pts) {
				// ��Ԃ�ǉ�
				registerOrGetFormat(curFormat);
				sectionFormatList.push_back(curFormat.formatId);
				startPtsList.push_back(curFromPTS);
				if (startPts == -1) {
					startPts = curFromPTS;
				}
				ctx.info("%.2f -> %d", (curFromPTS - startPts) / 90000.0, curFormat.formatId);
				curFromPTS = -1;
			}
			// �ύX�𔽉f
			switch (ev.type) {
			case PID_TABLE_CHANGED:
				if (curAudioFormats.size() < ev.numAudio) {
					curAudioFormats.resize(ev.numAudio);
				}
				if (curFormat.audioFormat.size() != ev.numAudio) {
					curFormat.audioFormat.resize(ev.numAudio);
					for (int i = 0; i < ev.numAudio; ++i) {
						curFormat.audioFormat[i] = curAudioFormats[i];
					}
					if (curFromPTS == -1) {
						curFromPTS = pts;
					}
				}
				break;
			case VIDEO_FORMAT_CHANGED:
				// �t�@�C���ύX
				++curFormat.videoFileId;
				videoFormatStartIndex_.push_back((int)outFormat_.size());
				curFormat.videoFormat = videoFrameList_[ev.frameIdx].format;
				// �f���t�H�[�}�b�g�̕ύX������D�悳����
				curFromPTS = dataPTS_[ev.frameIdx];
				break;
			case AUDIO_FORMAT_CHANGED:
				if (ev.audioIdx >= (int)curFormat.audioFormat.size()) {
					THROW(FormatException, "StreamEvent's audioIdx exceeds numAudio of the previous table change event");
				}
				curFormat.audioFormat[ev.audioIdx] = audioFrameList_[ev.frameIdx].format;
				curAudioFormats[ev.audioIdx] = audioFrameList_[ev.frameIdx].format;
				if (curFromPTS == -1) {
					curFromPTS = pts;
				}
				break;
			}
		}
		// �Ō�̋�Ԃ�ǉ�
		if (curFromPTS != -1) {
			registerOrGetFormat(curFormat);
			sectionFormatList.push_back(curFormat.formatId);
			startPtsList.push_back(curFromPTS);
			if (startPts == -1) {
				startPts = curFromPTS;
			}
			ctx.info("%.2f -> %d", (curFromPTS - startPts) / 90000.0, curFormat.formatId);
		}
		startPtsList.push_back(endPTS);
		videoFormatStartIndex_.push_back((int)outFormat_.size());

		// frameSectionId�𐶐�
		std::vector<int> outFormatFrames(outFormat_.size());
		std::vector<int> frameSectionId(videoFrameList_.size());
		for (int i = 0; i < int(videoFrameList_.size()); ++i) {
			double pts = modifiedPTS_[i];
			// ��Ԃ�T��
			int sectionId = int(std::partition_point(startPtsList.begin(), startPtsList.end(),
				[=](double sec) {
				return !(pts < sec);
			}) - startPtsList.begin() - 1);
			if (sectionId >= (int)sectionFormatList.size()) {
				THROWF(RuntimeException, "sectionId exceeds section count (%d >= %d) at frame %d",
					sectionId, (int)sectionFormatList.size(), i);
			}
			frameSectionId[i] = sectionId;
			outFormatFrames[sectionFormatList[sectionId]]++;
		}

		// �Z�N�V�������t�@�C���}�b�s���O�𐶐�
		std::vector<int> sectionFileList(sectionFormatList.size());

		if (splitSub) {
			// ���C���t�H�[�}�b�g�ȊO�͌������Ȃ� //

			int mainFormatId = int(std::max_element(
				outFormatFrames.begin(), outFormatFrames.end()) - outFormatFrames.begin());

			videoFileStartIndex_.push_back(0);
			for (int i = 0, mainFileId = -1, nextFileId = 0, videoId = 0;
				i < (int)sectionFormatList.size(); ++i)
			{
				int vid = outFormat_[sectionFormatList[i]].videoFileId;
				if (videoId != vid) {
					videoFileStartIndex_.push_back(nextFileId);
					videoId = vid;
				}
				if (sectionFormatList[i] == mainFormatId) {
					if (mainFileId == -1) {
						mainFileId = nextFileId++;
						fileFormatId_.push_back(mainFormatId);
					}
					sectionFileList[i] = mainFileId;
				}
				else {
					sectionFileList[i] = nextFileId++;
					fileFormatId_.push_back(sectionFormatList[i]);
				}
			}
			videoFileStartIndex_.push_back((int)fileFormatId_.size());
		}
		else {
			for (int i = 0; i < (int)sectionFormatList.size(); ++i) {
				// �t�@�C���ƃt�H�[�}�b�g�͓���
				sectionFileList[i] = sectionFormatList[i];
			}
			for (int i = 0; i < (int)outFormat_.size(); ++i) {
				// �t�@�C���ƃt�H�[�}�b�g�͍P���ϊ�
				fileFormatId_.push_back(i);
			}
			videoFileStartIndex_ = videoFormatStartIndex_;
		}

		// frameFileId_�𐶐�
		frameFileId_.resize(videoFrameList_.size());
		for (int i = 0; i < int(videoFrameList_.size()); ++i) {
			frameFileId_[i] = sectionFileList[frameSectionId[i]];
		}

		// �t�B���^�p���̓t���[�����X�g����
		filterFrameList_ = std::vector<std::vector<FilterSourceFrame>>(numVideoFile_);
		for (int videoId = 0; videoId < (int)numVideoFile_; ++videoId) {
			int keyFrame = -1;
			std::vector<FilterSourceFrame>& list = filterFrameList_[videoId];

			const auto& format = outFormat_[videoFormatStartIndex_[videoId]].videoFormat;
			double timePerFrame = format.frameRateDenom * MPEG_CLOCK_HZ / (double)format.frameRateNum;

			for (int i = 0; i < (int)videoFrameList_.size(); ++i) {
				int ordered = ordredVideoFrame_[i];
				int formatId = fileFormatId_[frameFileId_[ordered]];
				if (outFormat_[formatId].videoFileId == videoId) {

					double mPTS = modifiedPTS_[ordered];
					FileVideoFrameInfo& srcframe = videoFrameList_[ordered];
					if (srcframe.isGopStart) {
						keyFrame = int(list.size());
					}

					// �܂��L�[�t���[�����Ȃ��ꍇ�͎̂Ă�
					if (keyFrame == -1) continue;

					FilterSourceFrame frame;
					frame.halfDelay = false;
					frame.frameIndex = i;
					frame.pts = mPTS;
					frame.frameDuration = timePerFrame; // TODO: VFR�Ή�
					frame.framePTS = (int64_t)mPTS;
					frame.fileOffset = srcframe.fileOffset;
					frame.keyFrame = keyFrame;
					frame.cmType = CMTYPE_NONCM; // �ŏ��͑S��NonCM�ɂ��Ă���

					switch (srcframe.pic) {
					case PIC_FRAME:
					case PIC_TFF:
					case PIC_TFF_RFF:
						list.push_back(frame);
						break;
					case PIC_FRAME_DOUBLING:
						list.push_back(frame);
						frame.pts += timePerFrame;
						list.push_back(frame);
						break;
					case PIC_FRAME_TRIPLING:
						list.push_back(frame);
						frame.pts += timePerFrame;
						list.push_back(frame);
						frame.pts += timePerFrame;
						list.push_back(frame);
						break;
					case PIC_BFF:
						frame.halfDelay = true;
						frame.pts -= timePerFrame / 2;
						list.push_back(frame);
						break;
					case PIC_BFF_RFF:
						frame.halfDelay = true;
						frame.pts -= timePerFrame / 2;
						list.push_back(frame);
						frame.halfDelay = false;
						frame.pts += timePerFrame;
						list.push_back(frame);
						break;
					}
				}
			}
		}

		// indexAudioFrameList_���쐬
		int numMaxAudio = 1;
		for (int i = 0; i < (int)outFormat_.size(); ++i) {
			numMaxAudio = std::max(numMaxAudio, (int)outFormat_[i].audioFormat.size());
		}
		indexAudioFrameList_.resize(numMaxAudio);
		for (int i = 0; i < (int)audioFrameList_.size(); ++i) {
			indexAudioFrameList_[audioFrameList_[i].audioIdx].push_back(i);
		}

		// audioFileOffsets_�𐶐�
		audioFileOffsets_.resize(audioFrameList_.size() + 1);
		for (int i = 0; i < (int)audioFrameList_.size(); ++i) {
			audioFileOffsets_[i] = audioFrameList_[i].fileOffset;
		}
		const auto& lastFrame = audioFrameList_.back();
		audioFileOffsets_.back() = lastFrame.fileOffset + lastFrame.codedDataSize;

		// ���ԏ��
		srcTotalDuration_ = dataPTS_.back() - dataPTS_.front();
		if (timeList_.size() > 0) {
			auto ti = timeList_[0];
			// ���b�v�A���E���h���Ă�\��������̂ŏ�ʃr�b�g�͎̂ĂČv�Z
			double diff = (double)(int32_t(ti.first / 300 - dataPTS_.front())) / MPEG_CLOCK_HZ;
			tm t = tm();
			ti.second.getDay(t.tm_year, t.tm_mon, t.tm_mday);
			ti.second.getTime(t.tm_hour, t.tm_min, t.tm_sec);
			// ����
			t.tm_mon -= 1; // ����0�n�܂�Ȃ̂�
			t.tm_year -= 1900; // �N��1900������
			t.tm_hour -= 9; // ���{�Ȃ̂�GMT+9
			t.tm_sec -= (int)std::round(diff); // �ŏ��̃t���[���܂Ŗ߂�
			firstFrameTime_ = _mkgmtime(&t);
		}
	}

	void calcSizeAndTime()
	{
		// �e�t�@�C���̓��̓t�@�C�����ԂƃT�C�Y���v�Z
		// �\�[�X�t���[������
		fileSrcSize_ = std::vector<int64_t>(fileFormatId_.size(), 0);
		fileSrcDuration_ = std::vector<double>(fileFormatId_.size(), 0);
		for (int i = 0; i < (int)videoFrameList_.size(); ++i) {
			int ordered = ordredVideoFrame_[i];
			int fileId = frameFileId_[ordered];
			int next = (i + 1 < (int)videoFrameList_.size())
				? ordredVideoFrame_[i + 1]
				: -1;
			double duration = getSourceFrameDuration(ordered, next);

			const auto& frame = videoFrameList_[ordered];
			fileSrcSize_[fileId] += frame.codedDataSize;
			fileSrcDuration_[fileId] += duration;
		}

		// �e�t�@�C���̏o�̓t�@�C�����Ԃ��v�Z
		// CM����̓t�B���^���̓t���[���ɓK�p����Ă���̂Ńt�B���^���̓t���[������
		fileDuration_ = std::vector<std::array<double, CMTYPE_MAX>>(fileFormatId_.size(), std::array<double, CMTYPE_MAX>());
		for (int videoId = 0; videoId < (int)numVideoFile_; ++videoId) {
			std::vector<FilterSourceFrame>& list = filterFrameList_[videoId];
			for (int i = 0; i < (int)list.size(); ++i) {
				int fileId = frameFileId_[list[i].frameIndex];
				double duration = list[i].frameDuration;
				fileDuration_[fileId][CMTYPE_BOTH] += duration;
				fileDuration_[fileId][list[i].cmType] += duration;
			}
		}

		// ���v
		double sumDuration = 0;
		double maxDuration = 0;
		int maxId = 0;
		for (int i = 0; i < (int)fileFormatId_.size(); ++i) {
			double time = fileDuration_[i][CMTYPE_BOTH];
			sumDuration += time;
			if (maxDuration < time) {
				maxDuration = time;
				maxId = i;
			}
		}
		outTotalDuration_ = sumDuration;

		// �o�̓t�@�C���ԍ�����
		outFileIndex_.resize(fileFormatId_.size());
		outFileIndex_[maxId] = 0;
		for (int i = 0, cnt = 1; i < (int)fileFormatId_.size(); ++i) {
			if (i != maxId) {
				outFileIndex_[i] = cnt++;
			}
		}
	}

	template<typename I>
	void makeModifiedPTS(int64_t modifiedFirstPTS, std::vector<double>& modifiedPTS, const std::vector<I>& frames)
	{
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
				ctx.warn("PTS���߂��Ă��܂��B�����������ł��Ȃ���������܂���B [%d] %.0f -> %.0f",
					i, modifiedPTS[i - 1], modifiedPTS[i]);
			}
		}
	}

	void registerOrGetFormat(OutVideoFormat& format) {
		// ���łɂ���̂���T��
		for (int i = videoFormatStartIndex_.back(); i < (int)outFormat_.size(); ++i) {
			if (isEquealFormat(outFormat_[i], format)) {
				format.formatId = i;
				return;
			}
		}
		// �Ȃ��̂œo�^
		format.formatId = (int)outFormat_.size();
		outFormat_.push_back(format);
	}

	bool isEquealFormat(const OutVideoFormat& a, const OutVideoFormat& b) {
		if (a.videoFormat != b.videoFormat) return false;
		if (a.audioFormat.size() != b.audioFormat.size()) return false;
		for (int i = 0; i < (int)a.audioFormat.size(); ++i) {
			if (a.audioFormat[i] != b.audioFormat[i]) {
				return false;
			}
		}
		return true;
	}

	struct AudioState {
		double time = 0; // �ǉ����ꂽ�����t���[���̍��v����
		double lostPts = -1; // �����|�C���g����������PTS�i�\���p�j
		int lastFrame = -1;
	};

	struct OutFileState {
		int formatId; // �f�o�b�O�o�͗p
		double time; // �ǉ����ꂽ�f���t���[���̍��v����
		std::vector<AudioState> audioState;
		std::unique_ptr<FileAudioFrameList> audioFrameList;
	};

	AudioDiffInfo initAudioDiffInfo() {
		AudioDiffInfo adiff = AudioDiffInfo();
		adiff.totalSrcFrames = (int)audioFrameList_.size();
		adiff.basePts = dataPTS_[0];
		return adiff;
	}

	// �t�B���^���͂��特���\�z
	AudioDiffInfo genAudioStream()
	{
		AudioDiffInfo adiff = initAudioDiffInfo();

		std::vector<std::array<OutFileState, CMTYPE_MAX>> outFiles(fileFormatId_.size());

		// outFiles������
		for (int i = 0; i < (int)fileFormatId_.size(); ++i) {
			for (int c = 0; c < CMTYPE_MAX; ++c) {
				auto& file = outFiles[i][c];
				int numAudio = (int)outFormat_[fileFormatId_[i]].audioFormat.size();
				file.formatId = i;
				file.time = 0;
				file.audioState.resize(numAudio);
				file.audioFrameList =
					std::unique_ptr<FileAudioFrameList>(new FileAudioFrameList(numAudio));
			}
		}

		// �S�f���t���[����ǉ�
		ctx.info("[�����\�z]");
		for (int videoId = 0; videoId < numVideoFile_; ++videoId) {
			auto& frames = filterFrameList_[videoId];
			for (int i = 0; i < (int)frames.size(); ++i) {
				int ordered = ordredVideoFrame_[frames[i].frameIndex];
				int fileId = frameFileId_[ordered];
				addVideoFrame(outFiles[fileId][frames[i].cmType], fileId, frames[i].pts, frames[i].frameDuration, nullptr);
				addVideoFrame(outFiles[fileId][CMTYPE_BOTH], fileId, frames[i].pts, frames[i].frameDuration, &adiff);
			}
		}

		// �o�̓f�[�^����
		reformedAudioFrameList_.resize(fileFormatId_.size());
		for (int i = 0; i < (int)fileFormatId_.size(); ++i) {
			for (int c = 0; c < CMTYPE_MAX; ++c) {
				double time = outFiles[i][c].time;
				reformedAudioFrameList_[i][c] = std::move(outFiles[i][c].audioFrameList);
			}
		}

		return adiff;
	}

	AudioDiffInfo genWaveAudioStream()
	{
		AudioDiffInfo adiff = initAudioDiffInfo();

		// �S�f���t���[����ǉ�
		ctx.info("[CM����p�����\�z]");
		filterAudioFrameList_.resize(numVideoFile_);
		for (int videoId = 0; videoId < (int)numVideoFile_; ++videoId) {
			OutFileState file = { 0 };
			file.formatId = -1;
			file.time = 0;
			file.audioState.resize(1);
			file.audioFrameList =
				std::unique_ptr<FileAudioFrameList>(new FileAudioFrameList(1));

			auto& frames = filterFrameList_[videoId];
			auto& format = outFormat_[videoFormatStartIndex_[videoId]];
			double timePerFrame = format.videoFormat.frameRateDenom * MPEG_CLOCK_HZ / (double)format.videoFormat.frameRateNum;

			for (int i = 0; i < (int)frames.size(); ++i) {
				double endPts = frames[i].pts + timePerFrame;
				file.time += timePerFrame;

				// file.time�܂ŉ�����i�߂�
				auto& audioState = file.audioState[0];
				if (audioState.time < file.time) {
					double audioDuration = file.time - audioState.time;
					double audioPts = endPts - audioDuration;
					// �X�e���I�ɕϊ�����Ă���͂��Ȃ̂ŁA�����t�H�[�}�b�g�͖��Ȃ�
					fillAudioFrames(file, 0, nullptr, audioPts, audioDuration, &adiff);
				}
			}

			auto& list = file.audioFrameList->at(0);
			for (int i = 0; i < (int)list.size(); ++i) {
				FilterAudioFrame frame = { 0 };
				auto& info = audioFrameList_[list[i]];
				frame.frameIndex = list[i];
				frame.waveOffset = info.waveOffset;
				frame.waveLength = info.waveDataSize;
				filterAudioFrameList_[videoId].push_back(frame);
			}
		}

		return adiff;
	}

	// �t���[���̕\������
	// RFF: true=�\�[�X�t���[���̕\������ false=�t�B���^���̓t���[���iRFF�����ς݁j�̕\������
	template <bool RFF>
	double getFrameDuration(int index, int nextIndex)
	{
		const auto& videoFrame = videoFrameList_[index];
		int formatId = fileFormatId_[frameFileId_[index]];
		const auto& format = outFormat_[formatId];
		double frameDiff = format.videoFormat.frameRateDenom * MPEG_CLOCK_HZ / (double)format.videoFormat.frameRateNum;

		double duration;
		if (isVFR_) { // VFR
			if (nextIndex == -1) {
				duration = 0; // �Ō�̃t���[��
			}
			else {
				duration = modifiedPTS_[nextIndex] - modifiedPTS_[index];
			}
		}
		else { // CFR
			if (RFF) {
				switch (videoFrame.pic) {
				case PIC_FRAME:
				case PIC_TFF:
					duration = frameDiff;
					break;
				case PIC_TFF_RFF:
					duration = frameDiff * 1.5;
					break;
				case PIC_FRAME_DOUBLING:
					duration = frameDiff * 2;
					hasRFF_ = true;
					break;
				case PIC_FRAME_TRIPLING:
					duration = frameDiff * 3;
					hasRFF_ = true;
					break;
				case PIC_BFF:
					duration = frameDiff;
					break;
				case PIC_BFF_RFF:
					duration = frameDiff * 1.5;
					hasRFF_ = true;
					break;
				}
			}
			else {
				duration = frameDiff;
			}
		}

		return duration;
	}

	// �\�[�X�t���[���̕\������
	double getSourceFrameDuration(int index, int nextIndex) {
		return getFrameDuration<true>(index, nextIndex);
	}

	// �t�B���^���̓t���[���iRFF�����ς݁j�̕\������
	double getFilterSourceFrameDuration(int index, int nextIndex) {
		return getFrameDuration<false>(index, nextIndex);
	}

	void addVideoFrame(OutFileState& file, int fileId, double pts, double duration, AudioDiffInfo* adiff) {
		const auto& format = outFormat_[fileFormatId_[fileId]];
		double endPts = pts + duration;
		file.time += duration;

		ASSERT(format.audioFormat.size() == file.audioFrameList->size());
		ASSERT(format.audioFormat.size() == file.audioState.size());
		for (int i = 0; i < (int)format.audioFormat.size(); ++i) {
			// file.time�܂ŉ�����i�߂�
			auto& audioState = file.audioState[i];
			if (audioState.time >= file.time) {
				// �����͏\���i��ł�
				continue;
			}
			double audioDuration = file.time - audioState.time;
			double audioPts = endPts - audioDuration;
			fillAudioFrames(file, i, &format.audioFormat[i], audioPts, audioDuration, adiff);
		}
	}

	void fillAudioFrames(
		OutFileState& file, int index, // �Ώۃt�@�C���Ɖ����C���f�b�N�X
		const AudioFormat* format, // �����t�H�[�}�b�g
		double pts, double duration, // �J�n�C��PTS��90kHz�ł̃^�C���X�p��
		AudioDiffInfo* adiff)
	{
		auto& state = file.audioState[index];
		auto& outFrameList = file.audioFrameList->at(index);
		const auto& frameList = indexAudioFrameList_[index];

		fillAudioFramesInOrder(file, index, format, pts, duration, adiff);
		if (duration <= 0) {
			// �\���o�͂���
			return;
		}

		// ������������߂����炠�邩������Ȃ��̂ŒT���Ȃ���
		auto it = std::partition_point(frameList.begin(), frameList.end(), [&](int frameIndex) {
			double modPTS = modifiedAudioPTS_[frameIndex];
			double frameDuration = audioFrameDuration_[frameIndex];
			return modPTS + (frameDuration / 2) < pts;
		});
		if (it != frameList.end()) {
			// �������Ƃ���Ɉʒu���Z�b�g���ē���Ă݂�
			if (state.lostPts != pts) {
				state.lostPts = pts;
				if (adiff) {
					auto elapsed = elapsedTime(pts);
					ctx.debug("%d��%.3f�b�ŉ���%d-%d�̓����|�C���g�����������̂ōČ���",
						elapsed.first, elapsed.second, file.formatId, index);
				}
			}
			state.lastFrame = (int)(it - frameList.begin() - 1);
			fillAudioFramesInOrder(file, index, format, pts, duration, adiff);
		}

		// �L���ȉ����t���[����������Ȃ������ꍇ�͂Ƃ肠�����������Ȃ�
		// ���ɗL���ȉ����t���[�������������炻�̊Ԃ̓t���[�������������
		// �f����艹�����Z���Ȃ�\���͂��邪�A�L���ȉ������Ȃ��̂ł���Ύd���Ȃ���
		// ���Y������킯�ł͂Ȃ��̂Ŗ��Ȃ��Ǝv����

	}

	// lastFrame���珇�ԂɌ��ĉ����t���[��������
	void fillAudioFramesInOrder(
		OutFileState& file, int index, // �Ώۃt�@�C���Ɖ����C���f�b�N�X
		const AudioFormat* format, // �����t�H�[�}�b�g
		double& pts, double& duration, // �J�n�C��PTS��90kHz�ł̃^�C���X�p��
		AudioDiffInfo* adiff)
	{
		auto& state = file.audioState[index];
		auto& outFrameList = file.audioFrameList->at(index);
		const auto& frameList = indexAudioFrameList_[index];
		int nskipped = 0;

		for (int i = state.lastFrame + 1; i < (int)frameList.size(); ++i) {
			int frameIndex = frameList[i];
			const auto& frame = audioFrameList_[frameIndex];
			double modPTS = modifiedAudioPTS_[frameIndex];
			double frameDuration = audioFrameDuration_[frameIndex];
			double halfDuration = frameDuration / 2;
			double quaterDuration = frameDuration / 4;

			if (modPTS >= pts + duration) {
				// �J�n���I�������̏ꍇ
				if (modPTS >= pts + frameDuration - quaterDuration) {
					// �t���[����4����3�ȏ�̃Y���Ă���ꍇ
					// �s���߂�
					break;
				}
			}
			if (modPTS + (frameDuration / 2) < pts) {
				// �O������̂ŃX�L�b�v
				++nskipped;
				continue;
			}
			if (format != nullptr && frame.format != *format) {
				// �t�H�[�}�b�g���Ⴄ�̂ŃX�L�b�v
				continue;
			}

			// �󂫂�����ꍇ�̓t���[���𐅑�������
			// �t���[����4����3�ȏ�̋󂫂��ł���ꍇ�͖��߂�
			int nframes = (int)std::max(1.0, ((modPTS - pts) + (frameDuration / 4)) / frameDuration);

			if (adiff) {
				if (nframes > 1) {
					auto elapsed = elapsedTime(modPTS);
					ctx.debug("%d��%.3f�b�ŉ���%d-%d�ɂ��ꂪ����̂�%d�t���[��������",
						elapsed.first, elapsed.second, file.formatId, index, nframes - 1);
				}
				if (nskipped > 0) {
					if (state.lastFrame == -1) {
						ctx.debug("����%d-%d��%d�t���[���ڂ���J�n",
							file.formatId, index, nskipped);
					}
					else {
						auto elapsed = elapsedTime(modPTS);
						ctx.debug("%d��%.3f�b�ŉ���%d-%d�ɂ��ꂪ����̂�%d�t���[���X�L�b�v",
							elapsed.first, elapsed.second, file.formatId, index, nskipped);
					}
					nskipped = 0;
				}

				++adiff->totalUniquAudioFrames;
			}

			for (int t = 0; t < nframes; ++t) {
				// ���v���
				if (adiff) {
					double diff = std::abs(modPTS - pts);
					if (adiff->maxPtsDiff < diff) {
						adiff->maxPtsDiff = diff;
						adiff->maxPtsDiffPos = pts;
					}
					adiff->sumPtsDiff += diff;
					++adiff->totalAudioFrames;
				}

				// �t���[�����o��
				outFrameList.push_back(frameIndex);
				state.time += frameDuration;
				pts += frameDuration;
				duration -= frameDuration;
			}

			state.lastFrame = i;
			if (duration <= 0) {
				// �\���o�͂���
				return;
			}
		}
	}

	// �t�@�C���S�̂ł̎���
	std::pair<int, double> elapsedTime(double modPTS) const {
		double sec = (double)(modPTS - dataPTS_[0]) / MPEG_CLOCK_HZ;
		int minutes = (int)(sec / 60);
		sec -= minutes * 60;
		return std::make_pair(minutes, sec);
	}

	void genCaptionStream()
	{
		ctx.info("[�����\�z]");
		reformedCationList_.clear();
		reformedNicoJKList_.clear();

		for (int fileId = 0; fileId < (int)fileFormatId_.size(); ++fileId) {
			int videoId = outFormat_[fileFormatId_[fileId]].videoFileId;
			auto& frames = filterFrameList_[videoId];

			// �e�t���[���̎����𐶐�
			std::array<std::vector<double>, CMTYPE_MAX> frameTimes;
			std::array<double, CMTYPE_MAX> curTimes = { 0 };
			for (int i = 0; i < (int)frames.size(); ++i) {
				for (int c = 0; c < CMTYPE_MAX; ++c) {
					frameTimes[c].push_back(curTimes[c]);
				}
				if (frameFileId_[frames[i].frameIndex] == fileId) {
					curTimes[0] += frames[i].frameDuration;
					curTimes[frames[i].cmType] += frames[i].frameDuration;
				}
			}
			// �ŏI�t���[���̏I���������ǉ�
			for (int c = 0; c < CMTYPE_MAX; ++c) {
				frameTimes[c].push_back(curTimes[c]);
			}

			// �o�͎����𐶐�
			reformedCationList_.emplace_back();
			auto& clistItem = reformedCationList_.back();
			for (int c = 0; c < CMTYPE_MAX; ++c) {
				clistItem[c] = std::unique_ptr<OutCaptionList>(new OutCaptionList());
			}
			for (int i = 0; i < (int)captionItemList_.size(); ++i) {
				if (captionItemList_[i].line) { // �N���A�ȊO
					auto duration = captionDuration_[i];
					auto start = std::lower_bound(frames.begin(), frames.end(), duration.startPTS,
						[](const FilterSourceFrame& frame, double mid) { return frame.pts < mid; }) - frames.begin();
					auto end = std::lower_bound(frames.begin(), frames.end(), duration.endPTS,
						[](const FilterSourceFrame& frame, double mid) { return frame.pts < mid; }) - frames.begin();
					if (start < end) { // 1�t���[���ȏ�\�����Ԃ̂���ꍇ�̂�
						int langIndex = captionItemList_[i].langIndex;
						for (int c = 0; c < CMTYPE_MAX; ++c) {
							double startTime = frameTimes[c][start];
							double endTime = frameTimes[c][end];
							if (startTime < endTime) { // �\�����Ԃ̂���ꍇ�̂�
								if (langIndex >= clistItem[c]->size()) { // ���ꂪ����Ȃ��ꍇ�͍L����
									clistItem[c]->resize(langIndex + 1);
								}
								OutCaptionLine outcap = { startTime, endTime, captionItemList_[i].line.get() };
								clistItem[c]->at(langIndex).push_back(outcap);
							}
						}
					}
				}
			}

			// �j�R�j�R�����R�����g�𐶐�
			reformedNicoJKList_.emplace_back();
			auto& nlistItem = reformedNicoJKList_.back();
			for (int c = 0; c < CMTYPE_MAX; ++c) {
				nlistItem[c] = std::unique_ptr<NicoJKList>(new NicoJKList());
			}
			double tick = MPEG_CLOCK_HZ / 10;
			for (int t = 0; t < NICOJK_MAX; ++t) {
				auto& srcList = nicoJKList_[t];
				for (int i = 0; i < (int)srcList.size(); ++i) {
					auto item = srcList[i];
					auto start = std::lower_bound(frames.begin(), frames.end(), item.start,
						[](const FilterSourceFrame& frame, double mid) { return frame.pts < mid; }) - frames.begin();
					auto end = std::lower_bound(frames.begin(), frames.end(), item.end,
						[](const FilterSourceFrame& frame, double mid) { return frame.pts < mid; }) - frames.begin();
					// �J�n�����̃t�@�C���Ɋ܂܂�Ă��邩
					if (start < end && frameFileId_[frames[start].frameIndex] == fileId) {
						for (int c = 0; c < CMTYPE_MAX; ++c) {
							// �J�n������CM�^�C�v�Ɋ܂܂�Ă��邩
							if (c == 0 || frames[start].cmType == c) {
								double startTime = frameTimes[c][start];
								double endTime = frameTimes[c][end];
								NicoJKLine outcomment = { startTime, endTime, item.line };
								nlistItem[c]->at(t).push_back(outcomment);
							}
						}
					}
				}
			}
		}
	}
};

