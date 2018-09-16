/**
* Transcode manager
* Copyright (c) 2017-2018 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include <string>
#include <memory>
#include <limits>
#include <smmintrin.h>

#include "TsSplitter.hpp"
#include "Encoder.hpp"
#include "Muxer.hpp"
#include "StreamReform.hpp"
#include "LogoScan.hpp"
#include "CMAnalyze.hpp"
#include "InterProcessComm.hpp"
#include "CaptionData.hpp"
#include "CaptionFormatter.hpp"
#include "EncoderOptionParser.hpp"
#include "NicoJK.hpp"

class AMTSplitter : public TsSplitter {
public:
	AMTSplitter(AMTContext& ctx, const ConfigWrapper& setting)
		: TsSplitter(ctx, true, true, setting.isSubtitlesEnabled())
		, setting_(setting)
		, psWriter(ctx)
		, writeHandler(*this)
		, audioFile_(setting.getAudioFilePath(), _T("wb"))
		, waveFile_(setting.getWaveFilePath(), _T("wb"))
		, curVideoFormat_()
		, videoFileCount_(0)
		, videoStreamType_(-1)
		, audioStreamType_(-1)
		, audioFileSize_(0)
		, waveFileSize_(0)
		, srcFileSize_(0)
	{
		psWriter.setHandler(&writeHandler);
	}

	StreamReformInfo split()
	{
		readAll();

		// for debug
		printInteraceCount();

		return StreamReformInfo(ctx, videoFileCount_,
			videoFrameList_, audioFrameList_, captionTextList_, streamEventList_, timeList_);
	}

	int64_t getSrcFileSize() const {
		return srcFileSize_;
	}

	int64_t getTotalIntVideoSize() const {
		return writeHandler.getTotalSize();
	}

protected:
	class StreamFileWriteHandler : public PsStreamWriter::EventHandler {
		TsSplitter& this_;
		std::unique_ptr<File> file_;
		int64_t totalIntVideoSize_;
	public:
		StreamFileWriteHandler(TsSplitter& this_)
			: this_(this_), totalIntVideoSize_() { }
		virtual void onStreamData(MemoryChunk mc) {
			if (file_ != NULL) {
				file_->write(mc);
				totalIntVideoSize_ += mc.length;
			}
		}
		void open(const tstring& path) {
			totalIntVideoSize_ = 0;
			file_ = std::unique_ptr<File>(new File(path, _T("wb")));
		}
		void close() {
			file_ = nullptr;
		}
		int64_t getTotalSize() const {
			return totalIntVideoSize_;
		}
	};

	const ConfigWrapper& setting_;
	PsStreamWriter psWriter;
	StreamFileWriteHandler writeHandler;
	File audioFile_;
	File waveFile_;
	VideoFormat curVideoFormat_;

	int videoFileCount_;
	int videoStreamType_;
	int audioStreamType_;
	int64_t audioFileSize_;
	int64_t waveFileSize_;
	int64_t srcFileSize_;

	// �f�[�^
	std::vector<FileVideoFrameInfo> videoFrameList_;
	std::vector<FileAudioFrameInfo> audioFrameList_;
	std::vector<StreamEvent> streamEventList_;
	std::vector<CaptionItem> captionTextList_;
	std::vector<std::pair<int64_t, JSTTime>> timeList_;

	void readAll() {
		enum { BUFSIZE = 4 * 1024 * 1024 };
		auto buffer_ptr = std::unique_ptr<uint8_t[]>(new uint8_t[BUFSIZE]);
		MemoryChunk buffer(buffer_ptr.get(), BUFSIZE);
		File srcfile(setting_.getSrcFilePath(), _T("rb"));
		srcFileSize_ = srcfile.size();
		size_t readBytes;
		do {
			readBytes = srcfile.read(buffer);
			inputTsData(MemoryChunk(buffer.data, readBytes));
		} while (readBytes == buffer.length);
	}

	static bool CheckPullDown(PICTURE_TYPE p0, PICTURE_TYPE p1) {
		switch (p0) {
		case PIC_TFF:
		case PIC_BFF_RFF:
			return (p1 == PIC_TFF || p1 == PIC_TFF_RFF);
		case PIC_BFF:
		case PIC_TFF_RFF:
			return (p1 == PIC_BFF || p1 == PIC_BFF_RFF);
		default: // ����ȊO�̓`�F�b�N�ΏۊO
			return true;
		}
	}

	void printInteraceCount() {

		if (videoFrameList_.size() == 0) {
			ctx.error("�t���[��������܂���");
			return;
		}

		// ���b�v�A���E���h���Ȃ�PTS�𐶐�
		std::vector<std::pair<int64_t, int>> modifiedPTS;
		int64_t videoBasePTS = videoFrameList_[0].PTS;
		int64_t prevPTS = videoFrameList_[0].PTS;
		for (int i = 0; i < int(videoFrameList_.size()); ++i) {
			int64_t PTS = videoFrameList_[i].PTS;
			int64_t modPTS = prevPTS + int64_t((int32_t(PTS) - int32_t(prevPTS)));
			modifiedPTS.emplace_back(modPTS, i);
			prevPTS = modPTS;
		}

		// PTS�Ń\�[�g
		std::sort(modifiedPTS.begin(), modifiedPTS.end());

#if 0
		// �t���[�����X�g���o��
		FILE* framesfp = fopen("frames.txt", "w");
		fprintf(framesfp, "FrameNumber,DecodeFrameNumber,PTS,Duration,FRAME_TYPE,PIC_TYPE,IsGOPStart\n");
		for (int i = 0; i < (int)modifiedPTS.size(); ++i) {
			int64_t PTS = modifiedPTS[i].first;
			int decodeIndex = modifiedPTS[i].second;
			const VideoFrameInfo& frame = videoFrameList_[decodeIndex];
			int PTSdiff = -1;
			if (i < (int)modifiedPTS.size() - 1) {
				int64_t nextPTS = modifiedPTS[i + 1].first;
				const VideoFrameInfo& nextFrame = videoFrameList_[modifiedPTS[i + 1].second];
				PTSdiff = int(nextPTS - PTS);
				if (CheckPullDown(frame.pic, nextFrame.pic) == false) {
					ctx.warn("Flag Check Error: PTS=%lld %s -> %s",
						PTS, PictureTypeString(frame.pic), PictureTypeString(nextFrame.pic));
				}
			}
			fprintf(framesfp, "%d,%d,%lld,%d,%s,%s,%d\n",
				i, decodeIndex, PTS, PTSdiff, FrameTypeString(frame.type), PictureTypeString(frame.pic), frame.isGopStart ? 1 : 0);
		}
		fclose(framesfp);
#endif

		// PTS�Ԋu���o��
		struct Integer {
			int v;
			Integer() : v(0) { }
		};

		std::array<int, MAX_PIC_TYPE> interaceCounter = { 0 };
		std::map<int, Integer> PTSdiffMap;
		prevPTS = -1;
		for (const auto& ptsIndex : modifiedPTS) {
			int64_t PTS = ptsIndex.first;
			const VideoFrameInfo& frame = videoFrameList_[ptsIndex.second];
			interaceCounter[(int)frame.pic]++;
			if (prevPTS != -1) {
				int PTSdiff = int(PTS - prevPTS);
				PTSdiffMap[PTSdiff].v++;
			}
			prevPTS = PTS;
		}

		ctx.info("[�f���t���[�����v���]");

		int64_t totalTime = modifiedPTS.back().first - videoBasePTS;
		double sec = (double)totalTime / MPEG_CLOCK_HZ;
		int minutes = (int)(sec / 60);
		sec -= minutes * 60;
		ctx.infoF("����: %d��%.3f�b", minutes, sec);

		ctx.infoF("FRAME=%d DBL=%d TLP=%d TFF=%d BFF=%d TFF_RFF=%d BFF_RFF=%d",
			interaceCounter[0], interaceCounter[1], interaceCounter[2], interaceCounter[3], interaceCounter[4], interaceCounter[5], interaceCounter[6]);

		for (const auto& pair : PTSdiffMap) {
			ctx.infoF("(PTS_Diff,Cnt)=(%d,%d)", pair.first, pair.second.v);
		}
	}

	// TsSplitter���z�֐� //

	virtual void onVideoPesPacket(
		int64_t clock,
		const std::vector<VideoFrameInfo>& frames,
		PESPacket packet)
	{
		for (const VideoFrameInfo& frame : frames) {
			videoFrameList_.push_back(frame);
			videoFrameList_.back().fileOffset = writeHandler.getTotalSize();
		}
		psWriter.outVideoPesPacket(clock, frames, packet);
	}

	virtual void onVideoFormatChanged(VideoFormat fmt) {
		ctx.info("[�f���t�H�[�}�b�g�ύX]");

		StringBuilder sb;
		sb.append("�T�C�Y: %dx%d", fmt.width, fmt.height);
		if (fmt.width != fmt.displayWidth || fmt.height != fmt.displayHeight) {
			sb.append(" �\���̈�: %dx%d", fmt.displayWidth, fmt.displayHeight);
		}
		int darW, darH; fmt.getDAR(darW, darH);
		sb.append(" (%d:%d)", darW, darH);
		if (fmt.fixedFrameRate) {
			sb.append(" FPS: %d/%d", fmt.frameRateNum, fmt.frameRateDenom);
		}
		else {
			sb.append(" FPS: VFR");
		}
		ctx.info(sb.str().c_str());

		// �t�@�C���ύX
		if (!curVideoFormat_.isBasicEquals(fmt)) {
			// �A�X�y�N�g��ȊO���ύX����Ă�����t�@�C���𕪂���
			//�iStreamReform�Ə��������킹�Ȃ���΂Ȃ�Ȃ����Ƃɒ��Ӂj
			writeHandler.open(setting_.getIntVideoFilePath(videoFileCount_++));
			psWriter.outHeader(videoStreamType_, audioStreamType_);
		}
		curVideoFormat_ = fmt;

		StreamEvent ev = StreamEvent();
		ev.type = VIDEO_FORMAT_CHANGED;
		ev.frameIdx = (int)videoFrameList_.size();
		streamEventList_.push_back(ev);
	}

	virtual void onAudioPesPacket(
		int audioIdx,
		int64_t clock,
		const std::vector<AudioFrameData>& frames,
		PESPacket packet)
	{
		for (const AudioFrameData& frame : frames) {
			FileAudioFrameInfo info = frame;
			info.audioIdx = audioIdx;
			info.codedDataSize = frame.codedDataSize;
			info.waveDataSize = frame.decodedDataSize;
			info.fileOffset = audioFileSize_;
			info.waveOffset = waveFileSize_;
			audioFile_.write(MemoryChunk(frame.codedData, frame.codedDataSize));
			if (frame.decodedDataSize > 0) {
				waveFile_.write(MemoryChunk((uint8_t*)frame.decodedData, frame.decodedDataSize));
			}
			audioFileSize_ += frame.codedDataSize;
			waveFileSize_ += frame.decodedDataSize;
			audioFrameList_.push_back(info);
		}
		if (videoFileCount_ > 0) {
			psWriter.outAudioPesPacket(audioIdx, clock, frames, packet);
		}
	}

	virtual void onAudioFormatChanged(int audioIdx, AudioFormat fmt) {
		ctx.infoF("[����%d�t�H�[�}�b�g�ύX]", audioIdx);
		ctx.infoF("�`�����l��: %s �T���v�����[�g: %d",
			getAudioChannelString(fmt.channels), fmt.sampleRate);

		StreamEvent ev = StreamEvent();
		ev.type = AUDIO_FORMAT_CHANGED;
		ev.audioIdx = audioIdx;
		ev.frameIdx = (int)audioFrameList_.size();
		streamEventList_.push_back(ev);
	}

	virtual void onCaptionPesPacket(
		int64_t clock,
		std::vector<CaptionItem>& captions,
		PESPacket packet)
	{
		for (auto& caption : captions) {
			captionTextList_.emplace_back(std::move(caption));
		}
	}

	virtual DRCSOutInfo getDRCSOutPath(int64_t PTS, const std::string& md5) {
		DRCSOutInfo info;
		info.elapsed = (videoFrameList_.size() > 0) ? (double)(PTS - videoFrameList_[0].PTS) : -1.0;
		info.filename = setting_.getDRCSOutPath(md5);
		return info;
	}

	// TsPacketSelectorHandler���z�֐� //

	virtual void onPidTableChanged(const PMTESInfo video, const std::vector<PMTESInfo>& audio, const PMTESInfo caption) {
		// �x�[�X�N���X�̏���
		TsSplitter::onPidTableChanged(video, audio, caption);

		ASSERT(audio.size() > 0);
		videoStreamType_ = video.stype;
		audioStreamType_ = audio[0].stype;

		StreamEvent ev = StreamEvent();
		ev.type = PID_TABLE_CHANGED;
		ev.numAudio = (int)audio.size();
		ev.frameIdx = (int)videoFrameList_.size();
		streamEventList_.push_back(ev);
	}

	virtual void onTime(int64_t clock, JSTTime time) {
		timeList_.push_back(std::make_pair(clock, time));
	}
};

class EncoderArgumentGenerator
{
public:
	EncoderArgumentGenerator(
		const ConfigWrapper& setting,
		StreamReformInfo& reformInfo)
		: setting_(setting)
		, reformInfo_(reformInfo)
	{ }

	tstring GenEncoderOptions(
		int numFrames,
		VideoFormat outfmt,
		std::vector<BitrateZone> zones,
		double vfrBitrateScale,
		tstring timecodepath,
		bool is120fps,
		int videoFileIndex, int encoderIndex, CMType cmtype, int pass)
	{
		VIDEO_STREAM_FORMAT srcFormat = reformInfo_.getVideoStreamFormat();
		double srcBitrate = getSourceBitrate(videoFileIndex);
		return makeEncoderArgs(
			setting_.getEncoder(),
			setting_.getEncoderPath(),
			setting_.getOptions(
				numFrames,
				srcFormat, srcBitrate, false, pass, zones, vfrBitrateScale,
				videoFileIndex, encoderIndex, cmtype),
			outfmt,
			timecodepath,
			is120fps,
			setting_.getEncVideoFilePath(videoFileIndex, encoderIndex, cmtype));
	}

	double getSourceBitrate(int fileId) const
	{
		// �r�b�g���[�g�v�Z
		int64_t srcBytes = 0;
		double srcDuration = 0;
		int numEncoders = reformInfo_.getNumEncoders(fileId);
		for (int i = 0; i < numEncoders; ++i) {
			const auto& info = reformInfo_.getSrcVideoInfo(i, fileId);
			srcBytes += info.first;
			srcDuration += info.second;
		}
		return ((double)srcBytes * 8 / 1000) / ((double)srcDuration / MPEG_CLOCK_HZ);
	}

	EncodeFileInfo printBitrate(AMTContext& ctx, int videoFileIndex, CMType cmtype) const
	{
		double srcBitrate = getSourceBitrate(videoFileIndex);
		ctx.infoF("���͉f���r�b�g���[�g: %d kbps", (int)srcBitrate);
		VIDEO_STREAM_FORMAT srcFormat = reformInfo_.getVideoStreamFormat();
		double targetBitrate = std::numeric_limits<float>::quiet_NaN();
		if (setting_.isAutoBitrate()) {
			targetBitrate = setting_.getBitrate().getTargetBitrate(srcFormat, srcBitrate);
			if (cmtype == CMTYPE_CM) {
				targetBitrate *= setting_.getBitrateCM();
			}
			ctx.infoF("�ڕW�f���r�b�g���[�g: %d kbps", (int)targetBitrate);
		}
		EncodeFileInfo info;
		info.srcBitrate = srcBitrate;
		info.targetBitrate = targetBitrate;
		return info;
	}

private:
	const ConfigWrapper& setting_;
	const StreamReformInfo& reformInfo_;
};

static std::vector<BitrateZone> MakeBitrateZones(
	const std::vector<int>& frameDurations,
	const std::vector<EncoderZone>& cmzones,
	const ConfigWrapper& setting,
	VideoInfo outvi)
{
	std::vector<BitrateZone> bitrateZones;
	if (frameDurations.size() == 0 || setting.isEncoderSupportVFR()) {
		// VFR�łȂ��A�܂��́A�G���R�[�_��VFR���T�|�[�g���Ă��� -> VFR�p�ɒ�������K�v���Ȃ�
		for (int i = 0; i < (int)cmzones.size(); ++i) {
			bitrateZones.emplace_back(cmzones[i], setting.getBitrateCM());
		}
	}
	else {
		if (setting.isZoneAvailable()) {
			// VFR��Ή��G���R�[�_�Ń]�[���ɑΉ����Ă���΃r�b�g���[�g�]�[������
#if 0
			{
				File dump("zone_param.dat", "wb");
				dump.writeArray(frameDurations);
				dump.writeArray(cmzones);
				dump.writeValue(setting.getBitrateCM());
				dump.writeValue(outvi.fps_numerator);
				dump.writeValue(outvi.fps_denominator);
				dump.writeValue(setting.getX265TimeFactor());
				dump.writeValue(0.05);
			}
#endif
			return MakeVFRBitrateZones(
				frameDurations, cmzones, setting.getBitrateCM(),
				outvi.fps_numerator, outvi.fps_denominator,
				setting.getX265TimeFactor(), 0.05); // �S�̂�5%�܂ł̍��Ȃ狖�e����
		}
	}
	return bitrateZones;
}

#if 0
// �y�[�W�q�[�v���@�\���Ă��邩�e�X�g
void DoBadThing() {
	char *p = (char*)HeapAlloc(
		GetProcessHeap(),
		HEAP_GENERATE_EXCEPTIONS | HEAP_ZERO_MEMORY,
		8);
	memset(p, 'x', 32);
}
#endif

static void transcodeMain(AMTContext& ctx, const ConfigWrapper& setting)
{
#if 0
  MessageBox(NULL, "Debug", "Amatsukaze", MB_OK);
  //DoBadThing();
#endif

	const_cast<ConfigWrapper&>(setting).CreateTempDir();
	setting.dump();

	bool isNoEncode = (setting.getMode() == _T("cm"));

	auto eoInfo = ParseEncoderOption(setting.getEncoder(), setting.getEncoderOptions());
	PrintEncoderInfo(ctx, eoInfo);

	// �`�F�b�N
	if (!isNoEncode && !setting.isFormatVFRSupported() && eoInfo.afsTimecode) {
		THROW(FormatException, "M2TS/TS�o�͂�VFR���T�|�[�g���Ă��܂���");
	}

	ResourceManger rm(ctx, setting.getInPipe(), setting.getOutPipe());
	rm.wait(HOST_CMD_TSAnalyze);

	Stopwatch sw;
	sw.start();
	auto splitter = std::unique_ptr<AMTSplitter>(new AMTSplitter(ctx, setting));
	if (setting.getServiceId() > 0) {
		splitter->setServiceId(setting.getServiceId());
	}
	StreamReformInfo reformInfo = splitter->split();
	ctx.infoF("TS��͊���: %.2f�b", sw.getAndReset());
	int serviceId = splitter->getActualServiceId();
	int64_t numTotalPackets = splitter->getNumTotalPackets();
	int64_t numScramblePackets = splitter->getNumScramblePackets();
	int64_t totalIntVideoSize = splitter->getTotalIntVideoSize();
	int64_t srcFileSize = splitter->getSrcFileSize();
	splitter = nullptr;

	if (setting.isDumpStreamInfo()) {
		reformInfo.serialize(setting.getStreamInfoPath());
	}

	// �X�N�����u���p�P�b�g�`�F�b�N
	double scrambleRatio = (double)numScramblePackets / (double)numTotalPackets;
	if (scrambleRatio > 0.01) {
		ctx.errorF("%.2f%%�̃p�P�b�g���X�N�����u����Ԃł��B", scrambleRatio * 100);
		if (scrambleRatio > 0.3) {
			THROW(FormatException, "�X�N�����u���p�P�b�g���������܂�");
		}
	}

	if (!isNoEncode && setting.isIgnoreNoDrcsMap() == false) {
		// DRCS�}�b�s���O�`�F�b�N
		if (ctx.getErrorCount(AMT_ERR_NO_DRCS_MAP) > 0) {
			THROW(NoDrcsMapException, "�}�b�s���O�ɂȂ�DRCS�O�����萳��Ɏ��������ł��Ȃ��������ߏI�����܂�");
		}
	}

	reformInfo.prepare(setting.isSplitSub());

	time_t startTime = reformInfo.getFirstFrameTime();

	NicoJK nicoJK(ctx, setting);
	bool nicoOK = false;
	if (!isNoEncode && setting.isNicoJKEnabled()) {
		ctx.info("[�j�R�j�R�����R�����g�擾]");
		auto srcDuration = reformInfo.getInDuration() / MPEG_CLOCK_HZ;
		nicoOK = nicoJK.makeASS(serviceId, startTime, (int)srcDuration);
		if (nicoOK) {
			reformInfo.SetNicoJKList(nicoJK.getDialogues());
		}
		else {
			if (nicoJK.isFail() == false) {
				ctx.info("�Ή��`�����l��������܂���");
			}
			else if (setting.isIgnoreNicoJKError() == false) {
				THROW(RuntimeException, "�j�R�j�R�����R�����g�擾�Ɏ��s");
			}
		}
	}

	int numVideoFiles = reformInfo.getNumVideoFile();
	int numOutFiles = reformInfo.getNumOutFiles();
	std::vector<std::unique_ptr<CMAnalyze>> cmanalyze;

	// �\�[�X�t�@�C���ǂݍ��ݗp�f�[�^�ۑ�
	for (int videoFileIndex = 0; videoFileIndex < numVideoFiles; ++videoFileIndex) {
		// �t�@�C���ǂݍ��ݏ���ۑ�
		auto& fmt = reformInfo.getFormat(0, videoFileIndex);
		auto amtsPath = setting.getTmpAMTSourcePath(videoFileIndex);
		av::SaveAMTSource(amtsPath,
			setting.getIntVideoFilePath(videoFileIndex),
			setting.getWaveFilePath(),
			fmt.videoFormat, fmt.audioFormat[0],
			reformInfo.getFilterSourceFrames(videoFileIndex),
			reformInfo.getFilterSourceAudioFrames(videoFileIndex),
			setting.getDecoderSetting());
	}

	// ���S�ECM���
	rm.wait(HOST_CMD_CMAnalyze);
	sw.start();
	std::vector<std::pair<size_t, bool>> logoFound;
	for (int videoFileIndex = 0; videoFileIndex < numVideoFiles; ++videoFileIndex) {
		size_t numFrames = reformInfo.getFilterSourceFrames(videoFileIndex).size();
		// �`���v�^�[��͂�300�t���[���i��10�b�j�ȏ゠��ꍇ����
		//�i�Z������ƃG���[�ɂȂ邱�Ƃ�����̂Łj
		if (setting.isChapterEnabled() && numFrames >= 300)
		{
			int numFrames = (int)reformInfo.getFilterSourceFrames(videoFileIndex).size();
			cmanalyze.emplace_back(std::unique_ptr<CMAnalyze>(
				new CMAnalyze(ctx, setting, videoFileIndex, numFrames)));

			if (setting.isPmtCutEnabled()) {
				// PMT�ύX�ɂ��CM�ǉ��F��
				cmanalyze.back()->applyPmtCut(numFrames, setting.getPmtCutSideRate(),
					reformInfo.getPidChangedList(videoFileIndex));
			}

			logoFound.emplace_back(numFrames, cmanalyze.back()->getLogoPath().size() > 0);
			reformInfo.applyCMZones(videoFileIndex, cmanalyze.back()->getZones());

			// �`���v�^�[����
			ctx.info("[�`���v�^�[����]");
			MakeChapter makechapter(ctx, setting, reformInfo, videoFileIndex, cmanalyze.back()->getTrims());
			int numEncoders = reformInfo.getNumEncoders(videoFileIndex);
			for (int i = 0; i < numEncoders; ++i) {
				for (CMType cmtype : setting.getCMTypes()) {
					makechapter.exec(videoFileIndex, i, cmtype);
				}
			}
		}
		else {
			// �`���v�^�[CM��͖���
			cmanalyze.emplace_back(std::unique_ptr<CMAnalyze>(new CMAnalyze(ctx, setting)));
		}
	}
	if (setting.isChapterEnabled()) {
		// ���S�����������`�F�b�N //
		// �f���t�@�C�����t���[�����Ń\�[�g
		std::sort(logoFound.begin(), logoFound.end());
		if (setting.getLogoPath().size() > 0 && // ���S�w�肠��
			setting.isIgnoreNoLogo() == false &&          // ���S�Ȃ������łȂ�
			logoFound.back().first >= 300 &&
			logoFound.back().second == false)     // �ł������f���Ń��S��������Ȃ�����
		{
			THROW(NoLogoException, "�}�b�`���郍�S��������܂���ł���");
		}
		ctx.infoF("���S�ECM��͊���: %.2f�b", sw.getAndReset());
	}

	if (isNoEncode) {
		// CM��݂͂̂Ȃ炱���ŏI��
		return;
	}

	auto audioDiffInfo = reformInfo.genAudio();
	audioDiffInfo.printAudioPtsDiff(ctx);

	ctx.info("[�����t�@�C������]");
	for (int videoFileIndex = 0, currentEncoderFile = 0;
		videoFileIndex < numVideoFiles; ++videoFileIndex) {
		CaptionASSFormatter formatterASS(ctx);
		CaptionSRTFormatter formatterSRT(ctx);
		NicoJKFormatter formatterNicoJK(ctx);
		int numEncoders = reformInfo.getNumEncoders(videoFileIndex);
		for (int encoderIndex = 0; encoderIndex < numEncoders; ++encoderIndex, ++currentEncoderFile) {
			for (CMType cmtype : setting.getCMTypes()) {
				auto& capList = reformInfo.getOutCaptionList(encoderIndex, videoFileIndex, cmtype);
				for (int lang = 0; lang < capList.size(); ++lang) {
					WriteUTF8File(
						setting.getTmpASSFilePath(videoFileIndex, encoderIndex, lang, cmtype),
						formatterASS.generate(capList[lang]));
					WriteUTF8File(
						setting.getTmpSRTFilePath(videoFileIndex, encoderIndex, lang, cmtype),
						formatterSRT.generate(capList[lang]));
				}
				if (nicoOK) {
					const auto& headerLines = nicoJK.getHeaderLines();
					const auto& dialogues = reformInfo.getOutNicoJKList(encoderIndex, videoFileIndex, cmtype);
					for (NicoJKType jktype : setting.getNicoJKTypes()) {
						File file(setting.getTmpNicoJKASSPath(videoFileIndex, encoderIndex, cmtype, jktype), _T("w"));
						auto text = formatterNicoJK.generate(headerLines[(int)jktype], dialogues[(int)jktype]);
						file.write(MemoryChunk((uint8_t*)text.data(), text.size()));
					}
				}
			}
		}
	}
	ctx.infoF("�����t�@�C����������: %.2f�b", sw.getAndReset());

	auto argGen = std::unique_ptr<EncoderArgumentGenerator>(new EncoderArgumentGenerator(setting, reformInfo));
	std::vector<EncodeFileInfo> outFileInfo;
	std::vector<VideoFormat> outfiles;

	sw.start();
	for (int videoFileIndex = 0, currentEncoderFile = 0;
		videoFileIndex < numVideoFiles; ++videoFileIndex) {
		int numEncoders = reformInfo.getNumEncoders(videoFileIndex);
		if (numEncoders == 0) {
			ctx.warn("numEncoders == 0 ...");
		}
		else {
			const CMAnalyze* cma = cmanalyze[videoFileIndex].get();

			for (int encoderIndex = 0; encoderIndex < numEncoders; ++encoderIndex, ++currentEncoderFile) {
				for (CMType cmtype : setting.getCMTypes()) {

					// �o�͂�1�b�ȉ��Ȃ�X�L�b�v
					if (reformInfo.getFileDuration(encoderIndex, videoFileIndex, cmtype) < MPEG_CLOCK_HZ)
						continue;

					AMTFilterSource filterSource(ctx, setting, reformInfo,
						cma->getZones(), cma->getLogoPath(),
						videoFileIndex, encoderIndex, cmtype, rm);

					auto getTcPath = [&]() {
						return setting.getTimecodeFilePath(videoFileIndex, encoderIndex, cmtype);
					};

					try {
						PClip filterClip = filterSource.getClip();
						IScriptEnvironment2* env = filterSource.getEnv();
						auto encoderZones = filterSource.getZones();
						auto& outfmt = filterSource.getFormat();
						auto& outvi = filterClip->GetVideoInfo();
						auto& frameDurations = filterSource.getFrameDurations();
						FilterVFRProc vfrProc(ctx, frameDurations, outvi, setting.isVFR120fps());

						ctx.infoF("[�G���R�[�h�J�n] %d/%d %s", currentEncoderFile + 1, numOutFiles, CMTypeToString(cmtype));

						outFileInfo.push_back(argGen->printBitrate(ctx, videoFileIndex, cmtype));
						outfiles.push_back(outfmt);

						bool vfrEnabled = eoInfo.afsTimecode;

						if (vfrProc.isEnabled()) {
							// �t�B���^�ɂ��VFR���L��
							if (eoInfo.afsTimecode) {
								THROW(ArgumentException, "�G���R�[�_�ƃt�B���^�̗�����VFR�^�C���R�[�h���o�͂���Ă��܂��B");
							}
							else if (!setting.isFormatVFRSupported()) {
								THROW(FormatException, "M2TS/TS�o�͂�VFR���T�|�[�g���Ă��܂���");
							}
							vfrEnabled = true;
							// �]�[����VFR�t���[���ԍ��ɏC��
							vfrProc.toVFRZones(encoderZones);
							// �^�C���R�[�h����
							vfrProc.makeTimecode(getTcPath());
						}

						if (vfrEnabled) {
							outFileInfo.back().tcPath = getTcPath();
						}

						std::vector<int> pass;
						if (setting.isTwoPass()) {
							pass.push_back(1);
							pass.push_back(2);
						}
						else {
							pass.push_back(-1);
						}

						auto bitrateZones = MakeBitrateZones(frameDurations, encoderZones, setting, outvi);
						auto vfrBitrateScale = AdjustVFRBitrate(frameDurations);
						// VFR�t���[���^�C�~���O��120fps��
						bool is120fps = (eoInfo.afsTimecode || setting.isVFR120fps());
						std::vector<tstring> encoderArgs;
						for (int i = 0; i < (int)pass.size(); ++i) {
							encoderArgs.push_back(
								argGen->GenEncoderOptions(
									(frameDurations.size() > 0) ? (int)frameDurations.size() : outvi.num_frames,
									outfmt, bitrateZones, vfrBitrateScale, outFileInfo.back().tcPath, is120fps,
									videoFileIndex, encoderIndex, cmtype, pass[i]));
						}
						AMTFilterVideoEncoder encoder(ctx);
						encoder.encode(filterClip, outfmt,
							frameDurations, encoderArgs, env);
					}
					catch (const AvisynthError& avserror) {
						THROWF(AviSynthException, "%s", avserror.msg);
					}
				}
			}
		}
	}
	ctx.infoF("�G���R�[�h����: %.2f�b", sw.getAndReset());

	argGen = nullptr;

	rm.wait(HOST_CMD_Mux);
	sw.start();
	int64_t totalOutSize = 0;
	auto muxer = std::unique_ptr<AMTMuxder>(new AMTMuxder(ctx, setting, reformInfo));
	for (int videoFileIndex = 0, currentOutFile = 0;
		videoFileIndex < numVideoFiles; ++videoFileIndex)
	{
		int numEncoders = reformInfo.getNumEncoders(videoFileIndex);
		auto& cmtypes = setting.getCMTypes();
		auto& outFileMapping = reformInfo.getOutFileMapping();

		for (int encoderIndex = 0; encoderIndex < numEncoders; ++encoderIndex) {
			int outIndex = reformInfo.getOutFileIndex(encoderIndex, videoFileIndex);

			for (int i = 0; i < (int)cmtypes.size(); ++i) {
				CMType cmtype = cmtypes[i];

				// �o�͂�1�b�ȉ��Ȃ�X�L�b�v
				if (reformInfo.getFileDuration(encoderIndex, videoFileIndex, cmtype) < MPEG_CLOCK_HZ)
					continue;

				auto& info = outFileInfo[currentOutFile];
				const auto& vfmt = outfiles[currentOutFile];
				++currentOutFile;

				ctx.infoF("[Mux�J�n] %d/%d %s", outIndex + 1, reformInfo.getNumOutFiles(), CMTypeToString(cmtype));
				muxer->mux(
					videoFileIndex, encoderIndex, cmtype,
					vfmt, eoInfo, OutPathGenerator(setting,
						outFileMapping[outIndex], (i == 0) ? CMTYPE_BOTH : cmtype), nicoOK, info);

				totalOutSize += info.fileSize;
			}
		}
	}
	ctx.infoF("Mux����: %.2f�b", sw.getAndReset());

	muxer = nullptr;

	// �o�͌��ʂ�\��
	reformInfo.printOutputMapping([&](int index) { return setting.getOutFilePath(index, CMTYPE_BOTH); });

	// �o�͌���JSON�o��
	if (setting.getOutInfoJsonPath().size() > 0) {
		StringBuilder sb;
		sb.append("{ ")
			.append("\"srcpath\": \"%s\", ", toJsonString(setting.getSrcFilePath()))
			.append("\"outfiles\": [");
		for (int i = 0; i < (int)outFileInfo.size(); ++i) {
			if (i > 0) sb.append(", ");
			const auto& info = outFileInfo[i];
			sb.append("{ \"path\": \"%s\", \"srcbitrate\": %d, \"outbitrate\": %d, \"outfilesize\": %lld, ",
				toJsonString(info.outPath), (int)info.srcBitrate,
				std::isnan(info.targetBitrate) ? -1 : (int)info.targetBitrate, info.fileSize);
			sb.append("\"subs\": [");
			for (int s = 0; s < (int)info.outSubs.size(); ++s) {
				if (s > 0) sb.append(", ");
				sb.append("\"%s\"", toJsonString(info.outSubs[s]));
			}
			sb.append("] }");
		}
		sb.append("]")
			.append(", \"logofiles\": [");
		for (int i = 0; i < reformInfo.getNumVideoFile(); ++i) {
			if (i > 0) sb.append(", ");
			sb.append("\"%s\"", toJsonString(cmanalyze[i]->getLogoPath()));
		}
		sb.append("]")
			.append(", \"srcfilesize\": %lld, \"intvideofilesize\": %lld, \"outfilesize\": %lld",
				srcFileSize, totalIntVideoSize, totalOutSize);
		auto duration = reformInfo.getInOutDuration();
		sb.append(", \"srcduration\": %.3f, \"outduration\": %.3f",
			(double)duration.first / MPEG_CLOCK_HZ, (double)duration.second / MPEG_CLOCK_HZ);
		sb.append(", \"audiodiff\": ");
		audioDiffInfo.printToJson(sb);
		sb.append(", \"error\": {");
		for (int i = 0; i < AMT_ERR_MAX; ++i) {
			if (i > 0) sb.append(", ");
			sb.append("\"%s\": %d", AMT_ERROR_NAMES[i], ctx.getErrorCount((AMT_ERROR_COUNTER)i));
		}
		sb.append(" }");
		sb.append(", \"cmanalyze\": %s", (setting.isChapterEnabled() ? "true" : "false"))
			.append(", \"nicojk\": %s", (nicoOK ? "true" : "false"))
			.append(" }");

		std::string str = sb.str();
		MemoryChunk mc(reinterpret_cast<uint8_t*>(const_cast<char*>(str.data())), str.size());
		File file(setting.getOutInfoJsonPath(), _T("w"));
		file.write(mc);
	}
}

static void transcodeSimpleMain(AMTContext& ctx, const ConfigWrapper& setting)
{
	if (ends_with(setting.getSrcFilePath(), _T(".ts"))) {
		ctx.warn("��ʃt�@�C�����[�h�ł�TS�t�@�C���̏����͔񐄏��ł�");
	}

	auto encoder = std::unique_ptr<AMTSimpleVideoEncoder>(new AMTSimpleVideoEncoder(ctx, setting));
	encoder->encode();
	int audioCount = encoder->getAudioCount();
	int64_t srcFileSize = encoder->getSrcFileSize();
	VideoFormat videoFormat = encoder->getVideoFormat();
	encoder = nullptr;

	auto muxer = std::unique_ptr<AMTSimpleMuxder>(new AMTSimpleMuxder(ctx, setting));
	muxer->mux(videoFormat, audioCount);
	int64_t totalOutSize = muxer->getTotalOutSize();
	muxer = nullptr;

	// �o�͌��ʂ�\��
	ctx.info("����");
	if (setting.getOutInfoJsonPath().size() > 0) {
		StringBuilder sb;
		sb.append("{ \"srcpath\": \"%s\"", toJsonString(setting.getSrcFilePath()))
			.append(", \"outpath\": \"%s\"", toJsonString(setting.getOutFilePath(0, CMTYPE_BOTH)))
			.append(", \"srcfilesize\": %lld", srcFileSize)
			.append(", \"outfilesize\": %lld", totalOutSize)
			.append(" }");

		std::string str = sb.str();
		MemoryChunk mc(reinterpret_cast<uint8_t*>(const_cast<char*>(str.data())), str.size());
		File file(setting.getOutInfoJsonPath(), _T("w"));
		file.write(mc);
	}
}


class DrcsSearchSplitter : public TsSplitter {
public:
	DrcsSearchSplitter(AMTContext& ctx, const ConfigWrapper& setting)
		: TsSplitter(ctx, true, false, true)
		, setting_(setting)
	{ }

	void readAll()
	{
		enum { BUFSIZE = 4 * 1024 * 1024 };
		auto buffer_ptr = std::unique_ptr<uint8_t[]>(new uint8_t[BUFSIZE]);
		MemoryChunk buffer(buffer_ptr.get(), BUFSIZE);
		File srcfile(setting_.getSrcFilePath(), _T("rb"));
		size_t readBytes;
		do {
			readBytes = srcfile.read(buffer);
			inputTsData(MemoryChunk(buffer.data, readBytes));
		} while (readBytes == buffer.length);
	}

protected:
	const ConfigWrapper& setting_;
	std::vector<VideoFrameInfo> videoFrameList_;

	// TsSplitter���z�֐� //

	virtual void onVideoPesPacket(
		int64_t clock,
		const std::vector<VideoFrameInfo>& frames,
		PESPacket packet)
	{
		// ���̏��ŏ��̃t���[�������K�v�Ȃ�����
		for (const VideoFrameInfo& frame : frames) {
			videoFrameList_.push_back(frame);
		}
	}

	virtual void onVideoFormatChanged(VideoFormat fmt) { }

	virtual void onAudioPesPacket(
		int audioIdx,
		int64_t clock,
		const std::vector<AudioFrameData>& frames,
		PESPacket packet)
	{ }

	virtual void onAudioFormatChanged(int audioIdx, AudioFormat fmt) { }

	virtual void onCaptionPesPacket(
		int64_t clock,
		std::vector<CaptionItem>& captions,
		PESPacket packet)
	{ }

	virtual DRCSOutInfo getDRCSOutPath(int64_t PTS, const std::string& md5) {
		DRCSOutInfo info;
		info.elapsed = (videoFrameList_.size() > 0) ? (double)(PTS - videoFrameList_[0].PTS) : -1.0;
		info.filename = setting_.getDRCSOutPath(md5);
		return info;
	}

	virtual void onTime(int64_t clock, JSTTime time) { }
};

class SubtitleDetectorSplitter : public TsSplitter {
public:
	SubtitleDetectorSplitter(AMTContext& ctx, const ConfigWrapper& setting)
		: TsSplitter(ctx, true, false, true)
		, setting_(setting)
		, hasSubtltle_(false)
	{ }

	void readAll(int maxframes)
	{
		enum { BUFSIZE = 4 * 1024 * 1024 };
		auto buffer_ptr = std::unique_ptr<uint8_t[]>(new uint8_t[BUFSIZE]);
		MemoryChunk buffer(buffer_ptr.get(), BUFSIZE);
		File srcfile(setting_.getSrcFilePath(), _T("rb"));
		auto fileSize = srcfile.size();
		// �t�@�C���擪����10%�̂Ƃ��납��ǂ�
		srcfile.seek(fileSize / 10, SEEK_SET);
		int64_t totalRead = 0;
		// �Ō��10%�͓ǂ܂Ȃ�
		int64_t end = fileSize / 10 * 9;
		size_t readBytes;
		do {
			readBytes = srcfile.read(buffer);
			inputTsData(MemoryChunk(buffer.data, readBytes));
			totalRead += readBytes;
		} while (totalRead < end && !hasSubtltle_ && videoFrameList_.size() < maxframes);
	}

	bool getHasSubtitle() const {
		return hasSubtltle_;
	}

protected:
	const ConfigWrapper& setting_;
	std::vector<VideoFrameInfo> videoFrameList_;
	bool hasSubtltle_;

	// TsSplitter���z�֐� //

	virtual void onVideoPesPacket(
		int64_t clock,
		const std::vector<VideoFrameInfo>& frames,
		PESPacket packet)
	{
		// ���̏��ŏ��̃t���[�������K�v�Ȃ�����
		for (const VideoFrameInfo& frame : frames) {
			videoFrameList_.push_back(frame);
		}
	}

	virtual void onVideoFormatChanged(VideoFormat fmt) { }

	virtual void onAudioPesPacket(
		int audioIdx,
		int64_t clock,
		const std::vector<AudioFrameData>& frames,
		PESPacket packet)
	{ }

	virtual void onAudioFormatChanged(int audioIdx, AudioFormat fmt) { }

	virtual void onCaptionPesPacket(
		int64_t clock,
		std::vector<CaptionItem>& captions,
		PESPacket packet)
	{ }

	virtual DRCSOutInfo getDRCSOutPath(int64_t PTS, const std::string& md5) {
		return DRCSOutInfo();
	}

	virtual void onTime(int64_t clock, JSTTime time) { }

	virtual void onCaptionPacket(int64_t clock, TsPacket packet) {
		hasSubtltle_ = true;
	}
};

class AudioDetectorSplitter : public TsSplitter {
public:
	AudioDetectorSplitter(AMTContext& ctx, const ConfigWrapper& setting)
		: TsSplitter(ctx, true, true, false)
		, setting_(setting)
	{ }

	void readAll(int maxframes)
	{
		enum { BUFSIZE = 4 * 1024 * 1024 };
		auto buffer_ptr = std::unique_ptr<uint8_t[]>(new uint8_t[BUFSIZE]);
		MemoryChunk buffer(buffer_ptr.get(), BUFSIZE);
		File srcfile(setting_.getSrcFilePath(), _T("rb"));
		auto fileSize = srcfile.size();
		// �t�@�C���擪����10%�̂Ƃ��납��ǂ�
		srcfile.seek(fileSize / 10, SEEK_SET);
		int64_t totalRead = 0;
		// �Ō��10%�͓ǂ܂Ȃ�
		int64_t end = fileSize / 10 * 9;
		size_t readBytes;
		do {
			readBytes = srcfile.read(buffer);
			inputTsData(MemoryChunk(buffer.data, readBytes));
			totalRead += readBytes;
		} while (totalRead < end && videoFrameList_.size() < maxframes);
	}

protected:
	const ConfigWrapper& setting_;
	std::vector<VideoFrameInfo> videoFrameList_;

	// TsSplitter���z�֐� //

	virtual void onVideoPesPacket(
		int64_t clock,
		const std::vector<VideoFrameInfo>& frames,
		PESPacket packet)
	{
		// ���̏��ŏ��̃t���[�������K�v�Ȃ�����
		for (const VideoFrameInfo& frame : frames) {
			videoFrameList_.push_back(frame);
		}
	}

	virtual void onVideoFormatChanged(VideoFormat fmt) { }

	virtual void onAudioPesPacket(
		int audioIdx,
		int64_t clock,
		const std::vector<AudioFrameData>& frames,
		PESPacket packet)
	{ }

	virtual void onAudioFormatChanged(int audioIdx, AudioFormat fmt) {
		printf("�C���f�b�N�X: %d �`�����l��: %s �T���v�����[�g: %d\n",
			audioIdx, getAudioChannelString(fmt.channels), fmt.sampleRate);
	}

	virtual void onCaptionPesPacket(
		int64_t clock,
		std::vector<CaptionItem>& captions,
		PESPacket packet)
	{ }

	virtual DRCSOutInfo getDRCSOutPath(int64_t PTS, const std::string& md5) {
		return DRCSOutInfo();
	}

	virtual void onTime(int64_t clock, JSTTime time) { }
};

static void searchDrcsMain(AMTContext& ctx, const ConfigWrapper& setting)
{
	Stopwatch sw;
	sw.start();
	auto splitter = std::unique_ptr<DrcsSearchSplitter>(new DrcsSearchSplitter(ctx, setting));
	if (setting.getServiceId() > 0) {
		splitter->setServiceId(setting.getServiceId());
	}
	splitter->readAll();
	ctx.infoF("����: %.2f�b", sw.getAndReset());
}

static void detectSubtitleMain(AMTContext& ctx, const ConfigWrapper& setting)
{
	auto splitter = std::unique_ptr<SubtitleDetectorSplitter>(new SubtitleDetectorSplitter(ctx, setting));
	if (setting.getServiceId() > 0) {
		splitter->setServiceId(setting.getServiceId());
	}
	splitter->readAll(setting.getMaxFrames());
	printf("����%s\n", splitter->getHasSubtitle() ? "����" : "�Ȃ�");
}

static void detectAudioMain(AMTContext& ctx, const ConfigWrapper& setting)
{
	auto splitter = std::unique_ptr<AudioDetectorSplitter>(new AudioDetectorSplitter(ctx, setting));
	if (setting.getServiceId() > 0) {
		splitter->setServiceId(setting.getServiceId());
	}
	splitter->readAll(setting.getMaxFrames());
}
