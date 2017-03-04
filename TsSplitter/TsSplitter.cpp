// TsSplitter.cpp : DLL �A�v���P�[�V�����p�ɃG�N�X�|�[�g�����֐����`���܂��B
//

#include "common.h"

#include <algorithm>
#include <vector>
#include <map>
#include <array>

#include "StreamUtils.hpp"
#include "Mpeg2TsParser.hpp"
#include "Mpeg2VideoParser.hpp"
#include "H264VideoParser.hpp"
#include "AdtsParser.hpp"
#include "Mpeg2PsWriter.hpp"
#include "WaveWriter.h"

class VideoFrameParser : public TsSplitterObject, public PesParser {
public:
	VideoFrameParser(TsSplitterContext *ctx)
		: TsSplitterObject(ctx)
		, PesParser()
		, videoStreamFormat(VS_MPEG2)
		, parser(&mpeg2parser)
	{ }

	void setStreamFormat(VIDEO_STREAM_FORMAT streamFormat) {
		if (videoStreamFormat != streamFormat) {
			switch (streamFormat) {
			case VS_MPEG2:
				parser = &mpeg2parser;
				break;
			case VS_H264:
				parser = &h264parser;
				break;
			}
			reset();
			videoStreamFormat = streamFormat;
		}
	}

	VIDEO_STREAM_FORMAT getStreamFormat() { return videoStreamFormat; }

	void reset() {
		videoFormat = VideoFormat();
		parser->reset();
	}

protected:

	virtual void onPesPacket(int64_t clock, PESPacket packet) {
		if (clock == -1) {
			ctx->error("Video PES Packet �ɃN���b�N��񂪂���܂���");
			return;
		}
		if (!packet.has_PTS()) {
			ctx->error("Video PES Packet �� PTS ������܂���");
			return;
		}

		int64_t PTS = packet.has_PTS() ? packet.PTS : -1;
		int64_t DTS = packet.has_DTS() ? packet.DTS : PTS;
		MemoryChunk payload = packet.paylod();

		if (!parser->inputFrame(payload, frameInfo, PTS, DTS)) {
			ctx->error("�t���[�����̎擾�Ɏ��s PTS=%lld", PTS);
			return;
		}

		if (frameInfo.size() > 0) {
			const VideoFrameInfo& frame = frameInfo[0];

			if (frame.format.isEmpty()) {
				// �t�H�[�}�b�g���킩��Ȃ��ƃf�R�[�h�ł��Ȃ��̂ŗ����Ȃ�
				return;
			}

			if (frame.format != videoFormat) {
				// �t�H�[�}�b�g���ς����
				videoFormat = frame.format;
				onVideoFormatChanged(frame.format);
			}

			onVideoPesPacket(clock, frameInfo, packet);
		}
	}

	virtual void onVideoPesPacket(int64_t clock, const std::vector<VideoFrameInfo>& frames, PESPacket packet) = 0;

	virtual void onVideoFormatChanged(VideoFormat fmt) = 0;

private:
	VIDEO_STREAM_FORMAT videoStreamFormat;
	VideoFormat videoFormat;
	IVideoParser* parser;
	
	std::vector<VideoFrameInfo> frameInfo;

	MPEG2VideoParser mpeg2parser;
	H264VideoParser h264parser;

};

class AudioFrameParser : public TsSplitterObject, public PesParser {
public:
	AudioFrameParser(TsSplitterContext *ctx)
		: TsSplitterObject(ctx)
		, PesParser()
		, adtsParser(ctx)
	{ }

	virtual void onPesPacket(int64_t clock, PESPacket packet) {
		if (clock == -1) {
			ctx->error("Audio PES Packet �ɃN���b�N��񂪂���܂���");
			return;
		}
		if (!packet.has_PTS()) {
			ctx->error("Audio PES Packet �� PTS ������܂���");
			return;
		}

		int64_t PTS = packet.has_PTS() ? packet.PTS : -1;
		int64_t DTS = packet.has_DTS() ? packet.DTS : PTS;
		MemoryChunk payload = packet.paylod();

		adtsParser.inputFrame(payload, frameData, PTS);

		if (frameData.size() > 0) {
			const AudioFrameData& frame = frameData[0];

			if (frame.format != format) {
				// �t�H�[�}�b�g���ς����
				format = frame.format;
				onAudioFormatChanged(frame.format);
			}

			onAudioPesPacket(clock, frameData, packet);
		}
	}

	virtual void onAudioPesPacket(int64_t clock, const std::vector<AudioFrameData>& frames, PESPacket packet) = 0;

	virtual void onAudioFormatChanged(AudioFormat fmt) = 0;

private:
	AudioFormat format;

	std::vector<AudioFrameData> frameData;

	AdtsParser adtsParser;
};

// TS�X�g���[�������ʂ����߂��悤�ɂ���
class TsPacketBuffer : public TsPacketParser {
public:
	TsPacketBuffer(TsSplitterContext* ctx)
		: TsPacketParser(ctx)
		, handler(NULL)
		, numBefferedPackets_(0)
		, numMaxPackets(0)
		, buffering(false)
	{ }

	void setHandler(TsPacketHandler* handler) {
		this->handler = handler;
	}

	int numBefferedPackets() {
		return numBefferedPackets_;
	}

	void clearBuffer() {
		buffer.clear();
		numBefferedPackets_ = 0;
	}

	void setEnableBuffering(bool enable) {
		buffering = enable;
		if (!buffering) {
			clearBuffer();
		}
	}

	void setNumBufferingPackets(int numPackets) {
		numMaxPackets = numPackets;
	}

	void backAndInput() {
		if (handler != NULL) {
			for (int i = 0; i < buffer.size(); i += TS_PACKET_LENGTH) {
				handler->onTsPacket(-1, TsPacket(buffer.get() + i));
			}
		}
	}

	virtual void onTsPacket(TsPacket packet) {
		if (buffering) {
			if (numBefferedPackets_ >= numMaxPackets) {
				buffer.trimHead((numMaxPackets - numBefferedPackets_ + 1) * TS_PACKET_LENGTH);
				numBefferedPackets_ = numMaxPackets - 1;
			}
			buffer.add(packet.data, TS_PACKET_LENGTH);
			++numBefferedPackets_;
		}
		if (handler != NULL) {
			handler->onTsPacket(-1, packet);
		}
	}

private:
	TsPacketHandler* handler;
	AutoBuffer buffer;
	int numBefferedPackets_;
	int numMaxPackets;
	bool buffering;
};

class TsSystemClock {
public:
	TsSystemClock()
		: PcrPid(-1)
		, numPcrReceived(0)
		, numTotakPacketsReveived(0)
	{ }

	void setPcrPid(int PcrPid) {
		this->PcrPid = PcrPid;
	}

	// �\���Ȑ���PCR����M������
	bool pcrReceived() {
		return numPcrReceived >= 2;
	}

	// ���ݓ��͂��ꂽ�p�P�b�g����ɂ���relative������̃p�P�b�g�̓��͎�����Ԃ�
	int64_t getClock(int relative) {
		if (!pcrReceived()) {
			return -1;
		}
		int index = numTotakPacketsReveived + relative - 1;
		int64_t clockDiff = pcrInfo[1].clock - pcrInfo[0].clock;
		int64_t indexDiff = pcrInfo[1].packetIndex - pcrInfo[0].packetIndex;
		return clockDiff * (index - pcrInfo[1].packetIndex) / indexDiff + pcrInfo[1].clock;
	}

	// TS�X�g���[�����ŏ�����ǂݒ����Ƃ��ɌĂяo��
	void backTs() {
		numTotakPacketsReveived = 0;
	}

	// TS�X�g���[���̑S�f�[�^�����邱��
	void inputTsPacket(TsPacket packet) {
		if (packet.PID() == PcrPid) {
			if (packet.has_adaptation_field()) {
				MemoryChunk data = packet.adapdation_field();
				AdapdationField af(data.data, (int)data.length);
				if (af.parse() && af.check()) {
					if (af.discontinuity_indicator()) {
						// PCR���A���łȂ��̂Ń��Z�b�g
						numPcrReceived = 0;
					}
					if (pcrInfo[1].packetIndex < numTotakPacketsReveived) {
						std::swap(pcrInfo[0], pcrInfo[1]);
						if (af.PCR_flag()) {
							pcrInfo[1].clock = af.program_clock_reference;
							pcrInfo[1].packetIndex = numTotakPacketsReveived;
							++numPcrReceived;
						}

						// �e�X�g�p
						//if (pcrReceived()) {
						//	printf("PCR: %f Mbps\n", currentBitrate() / (1024 * 1024));
						//}
					}
				}
			}
		}
		++numTotakPacketsReveived;
	}

	double currentBitrate() {
		int clockDiff = int(pcrInfo[1].clock - pcrInfo[0].clock);
		int indexDiff = int(pcrInfo[1].packetIndex - pcrInfo[0].packetIndex);
		return (double)(indexDiff * TS_PACKET_LENGTH * 8) / clockDiff * 27000000;
	}

private:
	struct PCR_Info {
		int64_t clock;
		int packetIndex;
	};

	int PcrPid;
	int numPcrReceived;
	int numTotakPacketsReveived;
	PCR_Info pcrInfo[2];
};

class TsSplitter : public TsSplitterObject, private TsPacketSelectorHandler {
public:
	FILE* mpgfp;
	FILE* aacfp;
	FILE* wavfp;

	TsSplitter(TsSplitterContext *ctx)
		: TsSplitterObject(ctx)
		, initPhase(PMT_WAITING)
		, tsPacketHandler(*this)
		, pcrDetectionHandler(*this)
		, tsPacketParser(ctx)
		, tsPacketSelector(ctx)
		, videoParser(ctx, *this)
		, psWriter(ctx)
		, writeHandler(*this)
	{
		tsPacketParser.setHandler(&tsPacketHandler);
		tsPacketParser.setNumBufferingPackets(50 * 1024); // 9.6MB
		tsPacketSelector.setHandler(this);
		psWriter.setHandler(&writeHandler);
		reset();
	}

	void reset() {
		initPhase = PMT_WAITING;
		preferedServiceId = -1;
		selectedServiceId = -1;
		tsPacketParser.setEnableBuffering(true);
		allFrames.clear();
		inFrameCount = 0;
		outFrameCount = 0;
		numAudioSamples = 0;
	}

	void setServiceId(int sid) {
		preferedServiceId = sid;
	}

	int getActualServiceId(int sid) {
		return selectedServiceId;
	}

	void inputTsData(MemoryChunk data) {
		tsPacketParser.inputTS(data);
	}
	void flush() {
		tsPacketParser.flush();
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

		if (numAudioSamples > 0) {
			// �T���v�������m�肵���̂�WAV�w�b�_���X�V
			fseek(wavfp, 0, SEEK_SET);
			writeWaveHeader(wavfp, getNumAudioChannels(audioFormat.channels), audioFormat.sampleRate, 16, numAudioSamples);
		}

		if (allFrames.size() == 0) {
			printf("�t���[��������܂���");
			return;
		}

		// ���b�v�A���E���h���Ȃ�PTS�𐶐�
		std::vector<std::pair<int64_t, int>> modifiedPTS;
		int64_t videoBasePTS = allFrames[0].PTS;
		int64_t prevPTS = allFrames[0].PTS;
		for (int i = 0; i < int(allFrames.size()); ++i) {
			int64_t PTS = allFrames[i].PTS;
			int64_t modPTS = prevPTS + int64_t((int32_t(PTS) - int32_t(prevPTS)));
			modifiedPTS.push_back(std::make_pair(modPTS, i));
			prevPTS = PTS;
		}

		// PTS�Ń\�[�g
		std::sort(modifiedPTS.begin(), modifiedPTS.end());

		// �t���[�����X�g���o��
		FILE* framesfp = fopen("frames.txt", "w");
		fprintf(framesfp, "FrameNumber,DecodeFrameNumber,PTS,Duration,FRAME_TYPE,PIC_TYPE,IsGOPStart\n");
		for (int i = 0; i < (int)modifiedPTS.size(); ++i) {
			int64_t PTS = modifiedPTS[i].first;
			int decodeIndex = modifiedPTS[i].second;
			const VideoFrameInfo& frame = allFrames[decodeIndex];
			int PTSdiff = -1;
			if (i < (int)modifiedPTS.size() - 1) {
				int64_t nextPTS = modifiedPTS[i + 1].first;
				const VideoFrameInfo& nextFrame = allFrames[modifiedPTS[i + 1].second];
				PTSdiff = int(nextPTS - PTS);
				if (CheckPullDown(frame.pic, nextFrame.pic) == false) {
					printf("Flag Check Error: PTS=%lld %s -> %s\n",
						PTS, PictureTypeString(frame.pic), PictureTypeString(nextFrame.pic));
				}
			}
			fprintf(framesfp, "%d,%d,%lld,%d,%s,%s,%d\n",
				i, decodeIndex, PTS, PTSdiff, FrameTypeString(frame.type), PictureTypeString(frame.pic), frame.isGopStart ? 1 : 0);
		}
		fclose(framesfp);

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
			const VideoFrameInfo& frame = allFrames[ptsIndex.second];
			interaceCounter[(int)frame.pic]++;
			if (prevPTS != -1) {
				int PTSdiff = int(PTS - prevPTS);
				PTSdiffMap[PTSdiff].v++;
			}
			prevPTS = PTS;
		}

		int64_t totalTime = modifiedPTS.back().first - videoBasePTS;
		ctx->info("����: %f �b", totalTime / 90000.0);

		ctx->info("�t���[���J�E���^");
		ctx->info("FRAME=%d DBL=%d TLP=%d TFF=%d BFF=%d TFF_RFF=%d BFF_RFF=%d",
			interaceCounter[0], interaceCounter[1], interaceCounter[2], interaceCounter[3], interaceCounter[4], interaceCounter[5], interaceCounter[6]);

		for (const auto& pair : PTSdiffMap) {
			ctx->info("(PTS_Diff,Cnt)=(%d,%d)\n", pair.first, pair.second.v);
		}
	}

private:
	enum INITIALIZATION_PHASE {
		PMT_WAITING,	// PAT,PMT�҂�
		PCR_WAITING,	// �r�b�g���[�g�擾�̂���PCR2����M��
		INIT_FINISHED,	// �K�v�ȏ��͑�����
	};

	class SpTsPacketHandler : public TsPacketHandler {
		TsSplitter& this_;
	public:
		SpTsPacketHandler(TsSplitter& this_)
			: this_(this_) { }

		virtual void onTsPacket(int64_t clock, TsPacket packet) {
			this_.tsSystemClock.inputTsPacket(packet);

			int64_t packetClock = this_.tsSystemClock.getClock(0);
			this_.tsPacketSelector.inputTsPacket(packetClock, packet);
		}
	};
	class PcrDetectionHandler : public TsPacketHandler {
		TsSplitter& this_;
	public:
		PcrDetectionHandler(TsSplitter& this_)
			: this_(this_) { }

		virtual void onTsPacket(int64_t clock, TsPacket packet) {
			this_.tsSystemClock.inputTsPacket(packet);
			if (this_.tsSystemClock.pcrReceived()) {
				this_.ctx->debug("�K�v�ȏ��͎擾�����̂�TS���ŏ�����ǂݒ����܂�");
				this_.initPhase = INIT_FINISHED;
				// �n���h����߂��čŏ�����ǂݒ���
				this_.tsPacketParser.setHandler(&this_.tsPacketHandler);
				this_.tsSystemClock.backTs();
				this_.tsPacketParser.backAndInput();
				// �����K�v�Ȃ��̂Ńo�b�t�@�����O��OFF
				this_.tsPacketParser.setEnableBuffering(false);
			}
		}
	};
	class SpVideoFrameParser : public VideoFrameParser {
		TsSplitter& this_;
	public:
		SpVideoFrameParser(TsSplitterContext *ctx, TsSplitter& this_)
			: VideoFrameParser(ctx), this_(this_) { }

	protected:
		virtual void onVideoPesPacket(int64_t clock, const std::vector<VideoFrameInfo>& frames, PESPacket packet) {
			this_.onVideoPesPacket(clock, frames, packet);
		}

		virtual void onVideoFormatChanged(VideoFormat fmt) {
			this_.onVideoFormatChanged(fmt);
		}
	};
	class SpAudioFrameParser : public AudioFrameParser {
		TsSplitter& this_;
		int audioIdx;
	public:
		SpAudioFrameParser(TsSplitterContext *ctx, TsSplitter& this_, int audioIdx)
			: AudioFrameParser(ctx), this_(this_), audioIdx(audioIdx) { }

	protected:
		virtual void onAudioPesPacket(int64_t clock, const std::vector<AudioFrameData>& frames, PESPacket packet) {
			this_.onAudioPesPacket(audioIdx, clock, frames, packet);
		}

		virtual void onAudioFormatChanged(AudioFormat fmt) {
			this_.onAudioFormatChanged(audioIdx, fmt);
		}
	};
	class StreamFileWriteHandler : public PsStreamWriter::EventHandler {
		TsSplitter& this_;
	public:
		StreamFileWriteHandler(TsSplitter& this_)
			: this_(this_) { }

		virtual void onStreamData(MemoryChunk mc) {
			fwrite(mc.data, mc.length, 1, this_.mpgfp);
		}
	};

	INITIALIZATION_PHASE initPhase;

	TsPacketBuffer tsPacketParser;
	TsSystemClock tsSystemClock;
	SpTsPacketHandler tsPacketHandler;
	PcrDetectionHandler pcrDetectionHandler;
	TsPacketSelector tsPacketSelector;

	SpVideoFrameParser videoParser;
	std::vector<SpAudioFrameParser*> audioParsers;

	PsStreamWriter psWriter;
	StreamFileWriteHandler writeHandler;

	AutoBuffer buffer;

	int preferedServiceId;
	int selectedServiceId;


	// �e�X�g�p
	std::vector<VideoFrameInfo> allFrames;
	int64_t numAudioSamples;
	AudioFormat audioFormat;

	/////
	int inFrameCount;
	int outFrameCount;
	void onVideoPesPacket(int64_t clock, const std::vector<VideoFrameInfo>& frames, PESPacket packet) {
		for (const VideoFrameInfo& frame : frames) {
			allFrames.push_back(frame);
		}

		/*
		bool bSkip = false;
		if ((inFrameCount % 900) == 555) {
			printf("Skip %d frames [in] %d [out] %d\n", (int)frames.size(), inFrameCount, outFrameCount);
			bSkip = true;
		}
		*/
		inFrameCount += (int)frames.size();
		//if (bSkip) return;
		outFrameCount += (int)frames.size();

		psWriter.outVideoPesPacket(clock, frames, packet);
	}

	void onVideoFormatChanged(VideoFormat fmt) {
		ctx->debug("�f���t�H�[�}�b�g�ύX�����m");
		ctx->debug("�T�C�Y: %dx%d FPS: %d/%d", fmt.width, fmt.height, fmt.frameRateNum, fmt.frameRateDenom);
	}

	void onAudioPesPacket(int audioIdx, int64_t clock, const std::vector<AudioFrameData>& frames, PESPacket packet) {
		//
		for (const AudioFrameData& data : frames) {
			if (data.decodedDataSize > 0) {
				if (numAudioSamples == 0) {
					audioFormat = data.format;
					writeWaveHeader(wavfp, getNumAudioChannels(data.format.channels), data.format.sampleRate, 16, 0);
				}
				fwrite(data.decodedData, data.decodedDataSize, 1, wavfp);
				numAudioSamples += data.numDecodedSamples;
			}
			fwrite(data.codedData, data.codedDataSize, 1, aacfp);
		}

		psWriter.outAudioPesPacket(audioIdx, clock, frames, packet);

	}

	void onAudioFormatChanged(int audioIdx, AudioFormat fmt) {
		ctx->debug("���� %d �̃t�H�[�}�b�g�ύX�����m", audioIdx);
		ctx->debug("�`�����l��: %s �T���v�����[�g: %d",
			getAudioChannelString(fmt.channels), fmt.sampleRate);
	}

	// �T�[�r�X��ݒ肷��ꍇ�̓T�[�r�X��pids��ł̃C���f�b�N�X
	// �Ȃɂ����Ȃ��ꍇ�͕��̒l�̕Ԃ�
	virtual int onPidSelect(int TSID, const std::vector<int>& pids) {
		for (int i = 0; i < int(pids.size()); ++i) {
			if (preferedServiceId == pids[i]) {
				selectedServiceId = pids[i];
				ctx->info("[PAT�X�V] �T�[�r�X 0x%04x ��I��", selectedServiceId);
				return i;
			}
		}
		selectedServiceId = pids[0];
		ctx->info("[PAT�X�V] �T�[�r�X 0x%04x ��I���i�w�肪����܂���ł����j", selectedServiceId);
		return 0;
	}

	virtual void onPmtUpdated(int PcrPid) {
		if (initPhase == PMT_WAITING) {
			initPhase = PCR_WAITING;
			// PCR�n���h���ɒu��������TS���ŏ�����ǂݒ���
			tsPacketParser.setHandler(&pcrDetectionHandler);
			tsSystemClock.setPcrPid(PcrPid);
			tsSystemClock.backTs();
			tsPacketParser.backAndInput();
		}
	}

	// TsPacketSelector��PID Table���ύX���ꂽ���ύX��̏�񂪑�����
	virtual void onPidTableChanged(const PMTESInfo video, const std::vector<PMTESInfo>& audio) {
		// �f���X�g���[���`�����Z�b�g
		switch (video.stype) {
		case 0x02: // MPEG2-VIDEO
			videoParser.setStreamFormat(VS_MPEG2);
			break;
		case 0x1B: // H.264/AVC
			videoParser.setStreamFormat(VS_H264);
			break;
		}

		// �K�v�Ȑ����������p�[�T���
		size_t numAudios = audio.size();
		while (audioParsers.size() < numAudios) {
			int audioIdx = int(audioParsers.size());
			audioParsers.push_back(new SpAudioFrameParser(ctx, *this, audioIdx));
			ctx->debug("�����p�[�T %d ��ǉ�", audioIdx);
		}

		ASSERT(audio.size() > 0);
		psWriter.outHeader(video.stype, audio[0].stype);
	}

	virtual void onVideoPacket(int64_t clock, TsPacket packet) {
		videoParser.onTsPacket(clock, packet);
	}

	virtual void onAudioPacket(int64_t clock, TsPacket packet, int audioIdx) {
		ASSERT(audioIdx < audioParsers.size());
		audioParsers[audioIdx]->onTsPacket(clock, packet);
	}
};

