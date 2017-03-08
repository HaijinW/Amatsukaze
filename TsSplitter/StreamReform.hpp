#pragma once

#include <vector>
#include <map>
#include <memory>

#include "StreamUtils.hpp"

struct FileAudioFrameInfo : public AudioFrameInfo {
	int audioIdx;
	int codedDataSize;
	int64_t fileOffset;

	FileAudioFrameInfo()
		: AudioFrameInfo()
		, audioIdx(0)
		, codedDataSize(0)
		, fileOffset(0)
	{ }

	FileAudioFrameInfo(const AudioFrameInfo& info)
		: AudioFrameInfo(info)
		, audioIdx(0)
		, codedDataSize(0)
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

class StreamReformInfo : public AMTObject {
public:
	StreamReformInfo(
		AMTContext& ctx,
		int numVideoFile,
		std::vector<VideoFrameInfo>& videoFrameList,
		std::vector<FileAudioFrameInfo>& audioFrameList,
		std::vector<StreamEvent>& streamEventList)
		: AMTObject(ctx)
		, numVideoFile_(numVideoFile)
		, videoFrameList_(std::move(videoFrameList))
		, audioFrameList_(std::move(audioFrameList))
		, streamEventList_(std::move(streamEventList))
	{
		encodedFrames_.resize(videoFrameList_.size(), false);
	}

	void prepareEncode() {
		reformMain();
	}

	void prepareMux() {
		genAudioStream();
	}

	int getNumVideoFile() const {
		return numVideoFile_;
	}

	// PTS -> video frame index
	int getVideoFrameIndex(int64_t PTS, int videoFileIndex) const {
		auto it = framePtsMap_.find(PTS);
		if (it == framePtsMap_.end()) {
			return -1;
		}

		int frameIndex = it->second;

		// check videoFileIndex
		int formatId = frameFormatId_[frameIndex];
		int start = outFormatStartIndex_[videoFileIndex];
		int end = outFormatStartIndex_[videoFileIndex + 1];
		if (formatId < start || formatId >= end) {
			return -1;
		}

		return frameIndex;
	}

	int getNumEncoders(int videoFileIndex) const {
		return int(
			outFormatStartIndex_[videoFileIndex + 1] - outFormatStartIndex_[videoFileIndex]);
	}

	// video frame index -> VideoFrameInfo
	const VideoFrameInfo& getVideoFrameInfo(int frameIndex) const {
		return videoFrameList_[frameIndex];
	}

	// video frame index -> encoder index
	int getEncoderIndex(int frameIndex) const {
		int formatId = frameFormatId_[frameIndex];
		const auto& format = outFormat_[formatId];
		return formatId - outFormatStartIndex_[format.videoFileId];
	}

	// �t���[�����G���R�[�h�����t���O���Z�b�g
	void frameEncoded(int frameIndex) {
		encodedFrames_[frameIndex] = true;
	}

	const OutVideoFormat& getFormat(int encoderIndex, int videoFileIndex) const {
		int formatId = outFormatStartIndex_[videoFileIndex] + encoderIndex;
		return outFormat_[formatId];
	}

	const FileAudioFrameList& getFileAudioFrameList(
		int encoderIndex, int videoFileIndex) const
	{
		int formatId = outFormatStartIndex_[videoFileIndex] + encoderIndex;
		return *reformedAudioFrameList_[formatId].get();
	}

	// �e�t�@�C���̍Đ�����
	int64_t getFileDuration(int encoderIndex, int videoFileIndex) const {
		int formatId = outFormatStartIndex_[videoFileIndex] + encoderIndex;
		return fileDuration_[formatId];
	}

	const std::vector<int64_t>& getAudioFileOffsets() const {
		return audioFileOffsets_;
	}

	int getOutFileIndex(int encoderIndex, int videoFileIndex) const {
		int formatId = outFormatStartIndex_[videoFileIndex] + encoderIndex;
		return outFileIndex_[formatId];
	}

private:
	// 1st phase �o��
	int numVideoFile_;
	std::vector<VideoFrameInfo> videoFrameList_;
	std::vector<FileAudioFrameInfo> audioFrameList_;
	std::vector<StreamEvent> streamEventList_;

	// �v�Z�f�[�^
	std::vector<int64_t> modifiedPTS_; // ���b�v�A���E���h���Ȃ�PTS
	std::vector<int64_t> modifiedAudioPTS_; // ���b�v�A���E���h���Ȃ�PTS
	std::vector<int> audioFrameDuration_; // �e�����t���[���̎���
	std::vector<int> ordredVideoFrame_;
	std::vector<int64_t> dataPTS_; // �f���t���[���̃X�g���[����ł̈ʒu��PTS�̊֘A�t��
	std::vector<int64_t> streamEventPTS_;

	std::vector<std::vector<int>> indexAudioFrameList_; // �����C���f�b�N�X���Ƃ̃t���[�����X�g

	std::vector<OutVideoFormat> outFormat_;
	// ���ԉf���t�@�C�����Ƃ̃t�H�[�}�b�g�J�n�C���f�b�N�X
	// �T�C�Y�͒��ԉf���t�@�C����+1
	std::vector<int> outFormatStartIndex_;

	// 2nd phase ����
	std::vector<int> frameFormatId_; // videoFrameList_�Ɠ����T�C�Y
	std::map<int64_t, int> framePtsMap_;

	// 2nd phase �o��
	std::vector<bool> encodedFrames_;

	// 3rd phase ����
	std::vector<std::unique_ptr<FileAudioFrameList>> reformedAudioFrameList_;
	std::vector<int64_t> fileDuration_;
	std::vector<int64_t> audioFileOffsets_; // �����t�@�C���L���b�V���p
	std::vector<int> outFileIndex_;

	// ���Y�����
	int64_t sumPtsDiff_;
	int totalAudioFrames_; // �o�͂��������t���[���i�����������܂ށj
	int totalUniquAudioFrames_; // �o�͂��������t���[���i�����������܂܂��j
	int64_t maxPtsDiff_;
	int64_t maxPtsDiffPos_;

	void reformMain()
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

		// framePtsMap_���쐬�i�����ɍ���̂Łj
		for (int i = 0; i < int(videoFrameList_.size()); ++i) {
			framePtsMap_[videoFrameList_[i].PTS] = i;
		}

		// ���b�v�A���E���h���Ȃ�PTS�𐶐�
		makeModifiedPTS(modifiedPTS_, videoFrameList_);
		makeModifiedPTS(modifiedAudioPTS_, audioFrameList_);

		// audioFrameDuration_�𐶐�
		audioFrameDuration_.resize(audioFrameList_.size());
		for (int i = 0; i < (int)audioFrameList_.size(); ++i) {
			const auto& frame = audioFrameList_[i];
			audioFrameDuration_[i] = int((frame.numSamples * MPEG_CLOCK_HZ) / frame.format.sampleRate);
		}

		// ptsOrdredVideoFrame_�𐶐�
		ordredVideoFrame_.resize(videoFrameList_.size());
		for (int i = 0; i < (int)videoFrameList_.size(); ++i) {
			ordredVideoFrame_[i] = i;
		}
		std::sort(ordredVideoFrame_.begin(), ordredVideoFrame_.end(), [&](int a, int b) {
			return videoFrameList_[a].PTS < videoFrameList_[b].PTS;
		});

		// dataPTS�𐶐�
		// ��납�猩�Ă��̎��_�ōł�������PTS��dataPTS�Ƃ���
		int64_t curMin = INT64_MAX;
		int64_t curMax = 0;
		dataPTS_.resize(videoFrameList_.size());
		for (int i = (int)videoFrameList_.size() - 1; i >= 0; --i) {
			curMin = std::min(curMin, modifiedPTS_[i]);
			curMax = std::max(curMax, modifiedPTS_[i]);
			dataPTS_[i] = curMin;
		}

		// �X�g���[���C�x���g��PTS���v�Z
		int64_t exceedLastPTS = curMax + 1;
		streamEventPTS_.resize(streamEventList_.size());
		for (int i = 0; i < (int)streamEventList_.size(); ++i) {
			auto& ev = streamEventList_[i];
			int64_t pts = -1;
			if (ev.type == PID_TABLE_CHANGED || ev.type == VIDEO_FORMAT_CHANGED) {
				if (ev.frameIdx >= (int)videoFrameList_.size()) {
					// ���߂��đΏۂ̃t���[�����Ȃ�
					pts = exceedLastPTS;
				}
				else {
					pts = dataPTS_[ev.frameIdx];
				}
			}
			else if (ev.type == AUDIO_FORMAT_CHANGED) {
				if (ev.frameIdx >= (int)audioFrameList_.size()) {
					// ���߂��đΏۂ̃t���[�����Ȃ�
					pts = exceedLastPTS;
				}
				else {
					pts = audioFrameList_[ev.frameIdx].PTS;
				}
			}
			streamEventPTS_[i] = pts;
		}

		struct SingleFormatSection {
			int formatId;
			int64_t fromPTS, toPTS;
		};

		// ���ԓI�ɋ߂��X�g���[���C�x���g��1�̕ω��_�Ƃ݂Ȃ�
		const int64_t CHANGE_TORELANCE = 3 * MPEG_CLOCK_HZ;

		std::vector<SingleFormatSection> sectionList;

		OutVideoFormat curFormat = OutVideoFormat();
		SingleFormatSection curSection = SingleFormatSection();
		int64_t curFromPTS = -1;
		curFormat.videoFileId = -1;
		for (int i = 0; i < (int)streamEventList_.size(); ++i) {
			auto& ev = streamEventList_[i];
			int64_t pts = streamEventPTS_[i];
			if (pts >= exceedLastPTS) {
				// ���ɉf�����Ȃ���ΈӖ����Ȃ�
				continue;
			}
			if (curFromPTS == -1) { // �ŏ�
				curFromPTS = curSection.fromPTS = pts;
			}
			else if (curFromPTS + CHANGE_TORELANCE < pts) {
				// ��Ԃ�ǉ�
				curSection.toPTS = pts;
				registerOrGetFormat(curFormat);
				curSection.formatId = curFormat.formatId;
				sectionList.push_back(curSection);

				curFromPTS = curSection.fromPTS = pts;
			}
			// �ύX�𔽉f
			switch (ev.type) {
			case PID_TABLE_CHANGED:
				curFormat.audioFormat.resize(ev.numAudio);
				break;
			case VIDEO_FORMAT_CHANGED:
				// �t�@�C���ύX
				++curFormat.videoFileId;
				outFormatStartIndex_.push_back((int)outFormat_.size());
				curFormat.videoFormat = videoFrameList_[ev.frameIdx].format;
				break;
			case AUDIO_FORMAT_CHANGED:
				if (ev.audioIdx >= curFormat.audioFormat.size()) {
					THROW(FormatException, "StreamEvent's audioIdx exceeds numAudio of the previous table change event");
				}
				curFormat.audioFormat[ev.audioIdx] = audioFrameList_[ev.frameIdx].format;
				break;
			}
		}
		// �Ō�̋�Ԃ�ǉ�
		curSection.toPTS = exceedLastPTS;
		registerOrGetFormat(curFormat);
		curSection.formatId = curFormat.formatId;
		sectionList.push_back(curSection);
		outFormatStartIndex_.push_back((int)outFormat_.size());

		// frameFormatId_�𐶐�
		frameFormatId_.resize(videoFrameList_.size());
		for (int i = 0; i < int(videoFrameList_.size()); ++i) {
			int64_t pts = modifiedPTS_[i];
			// ��Ԃ�T��
			int formatId = std::partition_point(sectionList.begin(), sectionList.end(),
				[=](const SingleFormatSection& sec) {
				return !(pts < sec.toPTS);
			})->formatId;
			frameFormatId_[i] = formatId;
		}
	}

	template<typename I>
	static void makeModifiedPTS(std::vector<int64_t>& modifiedPTS, const std::vector<I>& frames)
	{
		// �O��̃t���[����PTS��6���Ԉȏ�̂��ꂪ����Ɛ����������ł��Ȃ�

		// ���b�v�A���E���h���Ȃ�PTS�𐶐�
		modifiedPTS.resize(frames.size());
		int64_t prevPTS = frames[0].PTS;
		for (int i = 0; i < int(frames.size()); ++i) {
			int64_t PTS = frames[i].PTS;
			int64_t modPTS = prevPTS + int64_t((int32_t(PTS) - int32_t(prevPTS)));
			modifiedPTS[i] = modPTS;
			prevPTS = PTS;
		}

		// �X�g���[�����߂��Ă���ꍇ�͏����ł��Ȃ��̂ŃG���[�Ƃ���
		for (int i = 1; i < int(frames.size()); ++i) {
			if (modifiedPTS[i] - modifiedPTS[i - 1] < -60 * MPEG_CLOCK_HZ) {
				// 1���ȏ�߂��Ă�����G���[�Ƃ���
				THROWF(FormatException,
					"PTS���߂��Ă��܂��B�����ł��܂���B %llu -> %llu",
					modifiedPTS[i - 1], modifiedPTS[i]);
			}
		}
	}

	void registerOrGetFormat(OutVideoFormat& format) {
		// ���łɂ���̂���T��
		for (int i = outFormatStartIndex_.back(); i < (int)outFormat_.size(); ++i) {
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

	int getEncoderIdx(const OutVideoFormat& format) {
		return format.formatId - outFormatStartIndex_[format.videoFileId];
	}

	static int numGenerateFrames(const VideoFrameInfo& frameInfo) {
		// BFF RFF����2���A����1��
		return (frameInfo.pic == PIC_BFF_RFF) ? 2 : 1;
	}

	struct AudioState {
		int64_t time = 0; // �ǉ����ꂽ�����t���[���̍��v����
		int lastFrame = -1;
	};

	struct OutFileState {
		int64_t time; // �ǉ����ꂽ�f���t���[���̍��v����
		std::vector<AudioState> audioState;
		std::unique_ptr<FileAudioFrameList> audioFrameList;
	};

	void genAudioStream() {

		// ���v��񏉊���
		sumPtsDiff_ = 0;
		totalAudioFrames_ = 0;
		totalUniquAudioFrames_ = 0;
		maxPtsDiff_ = 0;
		maxPtsDiffPos_ = 0;

		// indexAudioFrameList_���쐬
		int numMaxAudio = 1;
		for (int i = 0; i < (int)outFormat_.size(); ++i) {
			numMaxAudio = std::max(numMaxAudio, (int)outFormat_[i].audioFormat.size());
		}
		indexAudioFrameList_.resize(numMaxAudio);
		for (int i = 0; i < (int)audioFrameList_.size(); ++i) {
			indexAudioFrameList_[audioFrameList_[i].audioIdx].push_back(i);
		}

		std::vector<OutFileState> outFiles(outFormat_.size());

		// outFiles������
		for (int i = 0; i < (int)outFormat_.size(); ++i) {
			auto& file = outFiles[i];
			int numAudio = (int)outFormat_[i].audioFormat.size();
			file.time = 0;
			file.audioState.resize(numAudio);
			file.audioFrameList =
				std::unique_ptr<FileAudioFrameList>(new FileAudioFrameList(numAudio));
		}

		// �S�f���t���[����ǉ�
		for (int i = 0; i < (int)videoFrameList_.size(); ++i) {
			// �f���t���[����PTS���ɒǉ�����
			int ordered = ordredVideoFrame_[i];
			if (encodedFrames_[ordered]) {
				int formatId = frameFormatId_[ordered];
				addVideoFrame(outFiles[formatId], ordered);
			}
		}

		// �o�̓f�[�^����
		reformedAudioFrameList_.resize(outFormat_.size());
		fileDuration_.resize(outFormat_.size());
		int64_t maxDuration = 0;
		int maxId = 0;
		for (int i = 0; i < (int)outFormat_.size(); ++i) {
			int64_t time = outFiles[i].time;
			reformedAudioFrameList_[i] = std::move(outFiles[i].audioFrameList);
			fileDuration_[i] = time;
			if (maxDuration < time) {
				maxDuration = time;
				maxId = i;
			}
		}

		// audioFileOffsets_�𐶐�
		audioFileOffsets_.resize(audioFrameList_.size() + 1);
		for (int i = 0; i < (int)audioFrameList_.size(); ++i) {
			audioFileOffsets_[i] = audioFrameList_[i].fileOffset;
		}
		const auto& lastFrame = audioFrameList_.back();
		audioFileOffsets_.back() = lastFrame.fileOffset + lastFrame.codedDataSize;

		// �o�̓t�@�C���ԍ�����
		outFileIndex_.resize(outFormat_.size());
		outFileIndex_[maxId] = 0;
		for (int i = 0, cnt = 1; i < (int)outFormat_.size(); ++i) {
			if (i != maxId) {
				outFileIndex_[i] = cnt++;
			}
		}

		printAudioPtsDiff();
	}

	// �t�@�C���ɉf���t���[�����P���ǉ�
	void addVideoFrame(OutFileState& file, int index) {
		const auto& videoFrame = videoFrameList_[index];
		int64_t pts = modifiedPTS_[index];
		int formatId = frameFormatId_[index];
		const auto& format = outFormat_[formatId];
		int64_t duration = format.videoFormat.frameRateDenom * MPEG_CLOCK_HZ / format.videoFormat.frameRateNum;

		switch (videoFrame.pic) {
		case PIC_FRAME:
		case PIC_TFF:
		case PIC_TFF_RFF:
			break;
		case PIC_FRAME_DOUBLING:
			duration *= 2;
			break;
		case PIC_FRAME_TRIPLING:
			duration *= 3;
			break;
		case PIC_BFF:
			pts -= (duration / 2);
			break;
		case PIC_BFF_RFF:
			pts -= (duration / 2);
			duration *= 2;
			break;
		}

		int64_t endPts = pts + duration;
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
			int64_t audioDuration = file.time - audioState.time;
			int64_t audioPts = endPts - audioDuration;
			fillAudioFrames(file, i, format.audioFormat[i], audioPts, audioDuration);
		}
	}

	void fillAudioFrames(
		OutFileState& file, int index, // �Ώۃt�@�C���Ɖ����C���f�b�N�X
		const AudioFormat& format, // �����t�H�[�}�b�g
		int64_t pts, int64_t duration) // �J�n�C��PTS��90kHz�ł̃^�C���X�p��
	{
		auto& state = file.audioState[index];
		auto& outFrameList = file.audioFrameList->at(index);
		const auto& frameList = indexAudioFrameList_[index];

		fillAudioFramesInOrder(file, index, format, pts, duration);
		if (duration <= 0) {
			// �\���o�͂���
			return;
		}

		// ������������߂����炠�邩������Ȃ��̂ŒT���Ȃ���
		auto it = std::partition_point(frameList.begin(), frameList.end(), [&](int frameIndex) {
			int64_t modPTS = modifiedAudioPTS_[frameIndex];
			int frameDuration = audioFrameDuration_[frameIndex];
			return modPTS + (frameDuration / 2) < pts;
		});
		if (it != frameList.end()) {
			// �������Ƃ���Ɉʒu���Z�b�g���ē���Ă݂�
			ctx.warn("%f�b�ŉ���%d�̓����|�C���g�����������̂ōČ���",
				elapsedTime(pts), index);
			state.lastFrame = *it - 1;
			fillAudioFramesInOrder(file, index, format, pts, duration);
		}

		// �L���ȉ����t���[����������Ȃ������ꍇ�͂Ƃ肠�����������Ȃ�
		// ���ɗL���ȉ����t���[�������������炻�̊Ԃ̓t���[�������������
		// �f����艹�����Z���Ȃ�\���͂��邪�A�L���ȉ������Ȃ��̂ł���Ύd���Ȃ���
		// ���Y������킯�ł͂Ȃ��̂Ŗ��Ȃ��Ǝv����

	}

	// lastFrame���珇�ԂɌ��ĉ����t���[��������
	void fillAudioFramesInOrder(
		OutFileState& file, int index, // �Ώۃt�@�C���Ɖ����C���f�b�N�X
		const AudioFormat& format, // �����t�H�[�}�b�g
		int64_t& pts, int64_t& duration) // �J�n�C��PTS��90kHz�ł̃^�C���X�p��
	{
		auto& state = file.audioState[index];
		auto& outFrameList = file.audioFrameList->at(index);
		const auto& frameList = indexAudioFrameList_[index];
		int nskipped = 0;

		for (int i = state.lastFrame + 1; i < (int)frameList.size(); ++i) {
			int frameIndex = frameList[i];
			const auto& frame = audioFrameList_[frameIndex];
			int64_t modPTS = modifiedAudioPTS_[frameIndex];
			int frameDuration = audioFrameDuration_[frameIndex];

			if (modPTS >= pts + duration) {
				// �s���߂�
				break;
			}
			if (modPTS + (frameDuration / 2) < pts) {
				// �O������̂ŃX�L�b�v
				++nskipped;
				continue;
			}
			if (frame.format != format) {
				// �t�H�[�}�b�g���Ⴄ�̂ŃX�L�b�v
				continue;
			}

			// �󂫂�����ꍇ�̓t���[���𐅑�������
			// �t���[����4����3�ȏ�̋󂫂��ł���ꍇ�͖��߂�
			int nframes = std::max(1, ((int)(modPTS - pts) + (frameDuration / 4)) / frameDuration);

			if (nframes > 1) {
				ctx.warn("%f�b�ŉ���%d�ɂ��ꂪ����̂�%d�t���[�����������܂��B",
					elapsedTime(modPTS), index, nframes - 1);
			}
			if (nskipped > 0) {
				if (state.lastFrame == -1) {
					ctx.info("����%d��%d�t���[���ڂ���J�n���܂��B",
						index, nskipped);
				}
				else {
					ctx.warn("%f�b�ŉ���%d�ɂ��ꂪ����̂�%d�t���[���X�L�b�v���܂��B",
						elapsedTime(modPTS), index, nskipped);
				}
				nskipped = 0;
			}

			++totalUniquAudioFrames_;
			for (int t = 0; t < nframes; ++t) {
				// ���v���
				int64_t diff = std::abs(modPTS - pts);
				if (maxPtsDiff_ < diff) {
					maxPtsDiff_ = diff;
					maxPtsDiffPos_ = pts;
				}
				sumPtsDiff_ += diff;
				++totalAudioFrames_;

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

	void printAudioPtsDiff() {
		double avgDiff = (double)sumPtsDiff_ * 1000 / (totalAudioFrames_ * MPEG_CLOCK_HZ);
		double maxDiff = (double)maxPtsDiff_ * 1000 / MPEG_CLOCK_HZ;

		ctx.info("�����t���[�� %d�i����������%d�t���[���j�o�͂��܂����B",
			totalAudioFrames_, totalAudioFrames_ - totalUniquAudioFrames_);
		ctx.info("���Y���� ���� %.2fms �ő� %.2fms �ł��B",
			avgDiff, maxDiff);

		if (maxPtsDiff_ > 0 && maxDiff - avgDiff > 1) {
			ctx.info("�ő剹�Y���ʒu: ���͓���ŏ��̉f���t���[������%f�b��",
				elapsedTime(maxPtsDiffPos_));
		}
	}

	double elapsedTime(int64_t modPTS) {
		return (double)(modPTS - dataPTS_[0]) / MPEG_CLOCK_HZ;
	}
};

