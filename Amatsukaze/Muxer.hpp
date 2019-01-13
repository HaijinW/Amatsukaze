/**
* Call muxer
* Copyright (c) 2017-2018 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include "StreamUtils.hpp"
#include "TranscodeSetting.hpp"
#include "StreamReform.hpp"
#include "PacketCache.hpp"
#include "EncoderOptionParser.hpp"
#include "AdtsParser.hpp"
#include "ProcessThread.hpp"

struct EncodeFileOutput {
	bool vfrEnabled;
	VideoFormat vfmt;
	std::vector<tstring> outSubs; // �O���t�@�C���ŏo�͂��ꂽ����
	int64_t fileSize;
	double srcBitrate;
	double targetBitrate;
};

class AMTMuxder : public AMTObject {
public:
	AMTMuxder(
		AMTContext&ctx,
		const ConfigWrapper& setting,
		const StreamReformInfo& reformInfo)
		: AMTObject(ctx)
		, setting_(setting)
		, reformInfo_(reformInfo)
		, audioCache_(ctx, setting.getAudioFilePath(), reformInfo.getAudioFileOffsets(), 12, 4)
	{ }

	void mux(EncodeFileKey key,
		const EncoderOptionInfo& eoInfo, // �G���R�[�_�I�v�V�������
		bool nicoOK,
		EncodeFileOutput& fileOut) // �o�͏��
	{
		const auto& fileIn = reformInfo_.getEncodeFile(key);
		auto fmt = reformInfo_.getFormat(key);
		auto vfmt = fileOut.vfmt;

		if (eoInfo.selectEvery > 1) {
			// �G���R�[�_�ŊԈ����ꍇ������̂ŁA����𔽉f����
			vfmt.mulDivFps(1, eoInfo.selectEvery);
		}

		if (vfmt.progressive == false) {
			// �G���R�[�_�ŃC���^���������Ă���ꍇ������̂ŁA����𔽉f����
			switch (eoInfo.deint) {
			case ENCODER_DEINT_24P:
				vfmt.mulDivFps(4, 5);
				vfmt.progressive = true;
				break;
			case ENCODER_DEINT_30P:
			case ENCODER_DEINT_VFR:
				vfmt.progressive = true;
				break;
			case ENCODER_DEINT_60P:
				vfmt.mulDivFps(2, 1);
				vfmt.progressive = true;
				break;
			}
		}
		else {
			if (eoInfo.deint != ENCODER_DEINT_NONE) {
				// �ꉞ�x�����o��
				ctx.warn("�G���R�[�_�ւ̓��͂̓v���O���b�V�u�ł����A"
					"�G���R�[�_�I�v�V�����ŃC���^�������w�肪����Ă��܂��B");
				ctx.warn("�G���R�[�_�ł��̃I�v�V���������������ꍇ�͖�肠��܂���B");
			}
		}

		// �����t�@�C�����쐬
		std::vector<tstring> audioFiles;
		for (int asrc = 0, adst = 0; asrc < (int)fileIn.audioFrames.size(); ++asrc) {
			const std::vector<int>& frameList = fileIn.audioFrames[asrc];
			if (frameList.size() > 0) {
				if (fmt.audioFormat[asrc].channels == AUDIO_2LANG) {
					// �f���A�����m��2��AAC�ɕ���
					ctx.infoF("����%d-%d�̓f���A�����m�Ȃ̂�2��AAC�t�@�C���ɕ������܂�", fileIn.outKey.format, asrc);
					SpDualMonoSplitter splitter(ctx);
					tstring filepath0 = setting_.getIntAudioFilePath(key, adst++);
					tstring filepath1 = setting_.getIntAudioFilePath(key, adst++);
					splitter.open(0, filepath0);
					splitter.open(1, filepath1);
					for (int frameIndex : frameList) {
						splitter.inputPacket(audioCache_[frameIndex]);
					}
					audioFiles.push_back(filepath0);
					audioFiles.push_back(filepath1);
				}
				else {
					tstring filepath = setting_.getIntAudioFilePath(key, adst++);
					File file(filepath, _T("wb"));
					for (int frameIndex : frameList) {
						file.write(audioCache_[frameIndex]);
					}
					audioFiles.push_back(filepath);
				}
			}
		}

		// �f���t�@�C��
		tstring encVideoFile;
		encVideoFile = setting_.getEncVideoFilePath(key);

		// �`���v�^�[�t�@�C��
		tstring chapterFile;
		if (setting_.isChapterEnabled()) {
			auto path = setting_.getTmpChapterPath(key);
			if (File::exists(path)) {
				chapterFile = path;
			}
		}

		// �����t�@�C��
		std::vector<tstring> subsFiles;
		std::vector<tstring> subsTitles;
		if (nicoOK) {
			for (NicoJKType jktype : setting_.getNicoJKTypes()) {
				auto srcsub = setting_.getTmpNicoJKASSPath(key, jktype);
				if (setting_.getFormat() == FORMAT_MKV) {
					subsFiles.push_back(srcsub);
					subsTitles.push_back(StringFormat(_T("NicoJK%s"), GetNicoJKSuffix(jktype)));
				}
				else { // MP4�̏ꍇ�͕ʃt�@�C���Ƃ��ăR�s�[
					auto dstsub = setting_.getOutASSPath(fileIn .outKey, fileIn .keyMax, -1, jktype);
					File::copy(srcsub, dstsub);
					fileOut.outSubs.push_back(dstsub);
				}
			}
		}
		for (int lang = 0; lang < fileIn.captionList.size(); ++lang) {
			auto srcass = setting_.getTmpASSFilePath(key, lang);
			if (setting_.getFormat() == FORMAT_MKV) {
				subsFiles.push_back(srcass);
				subsTitles.push_back(_T("ASS"));
			}
			else { // MP4,M2TS�̏ꍇ�͕ʃt�@�C���Ƃ��ăR�s�[
				auto dstsub = setting_.getOutASSPath(fileIn.outKey, fileIn.keyMax, lang, (NicoJKType)0);
				File::copy(srcass, dstsub);
				fileOut.outSubs.push_back(dstsub);
			}
			auto srcsrt = setting_.getTmpSRTFilePath(key, lang);
			if (File::exists(srcsrt)) {
				// SRT�͋ɋH�ɏo�͂���Ȃ����Ƃ����邱�Ƃɒ���
				subsFiles.push_back(srcsrt);
				subsTitles.push_back(_T("SRT"));
			}
		}

		// �^�C���R�[�h�p
		bool is120fps = (eoInfo.afsTimecode || setting_.isVFR120fps());
		std::pair<int, int> timebase = std::make_pair(vfmt.frameRateNum * (is120fps ? 4 : 2), vfmt.frameRateDenom);

		tstring tmpOutPath = setting_.getVfrTmpFilePath(key);

		tstring metaFile;
		if (setting_.getFormat() == FORMAT_M2TS || setting_.getFormat() == FORMAT_TS) {
			// M2TS/TS�̏ꍇ��meta�t�@�C���쐬
			StringBuilder sb;
			sb.append("MUXOPT\n");
			switch (eoInfo.format) {
			case VS_MPEG2:
				sb.append("V_MPEG-2");
				break;
			case VS_H264:
				sb.append("V_MPEG4/ISO/AVC");
				break;
			case VS_H265:
				sb.append("V_MPEGH/ISO/HEVC");
				break;
			}
			double fps = vfmt.frameRateNum / (double)vfmt.frameRateDenom;
			sb.append(", \"%s\", fps=%.3f\n", encVideoFile, fps);
			for (auto apath : audioFiles) {
				sb.append("A_AAC, \"%s\"\n", apath);
			}
			for (auto spath : subsFiles) {
				sb.append("S_TEXT/UTF8, \"%s\", fps=%.3f, video-width=%d, video-height=%d\n",
					spath, fps, vfmt.width, vfmt.height);
			}

			metaFile = setting_.getM2tsMetaFilePath(key);
			File file(metaFile, _T("w"));
			file.write(sb.getMC());
		}

		auto outPath = setting_.getOutFilePath(fileIn.outKey, fileIn.keyMax);
		auto args = makeMuxerArgs(
			setting_.getFormat(),
			setting_.getMuxerPath(), setting_.getTimelineEditorPath(), setting_.getMp4BoxPath(),
			encVideoFile, vfmt, audioFiles,
			outPath, tmpOutPath, chapterFile,
      fileOut.vfrEnabled ? setting_.getTimecodeFilePath(key) : tstring(),
      timebase, subsFiles, subsTitles, metaFile);

		for (int i = 0; i < (int)args.size(); ++i) {
			ctx.infoF("%s", args[i].first);
			StdRedirectedSubProcess muxer(args[i].first, 0, args[i].second);
			int ret = muxer.join();
			if (ret != 0) {
				THROWF(RuntimeException, "mux failed (exit code: %d)", ret);
			}
			// mp4box���R���\�[���o�͂̃R�[�h�y�[�W��ς��Ă��܂��̂Ŗ߂�
			ctx.setDefaultCP();
		}

		File outfile(outPath, _T("rb"));
		fileOut.fileSize = outfile.size();
	}

private:
	class SpDualMonoSplitter : public DualMonoSplitter
	{
		std::unique_ptr<File> file[2];
	public:
		SpDualMonoSplitter(AMTContext& ctx) : DualMonoSplitter(ctx) { }
		void open(int index, const tstring& filename) {
			file[index] = std::unique_ptr<File>(new File(filename, _T("wb")));
		}
		virtual void OnOutFrame(int index, MemoryChunk mc) {
			file[index]->write(mc);
		}
	};

	const ConfigWrapper& setting_;
	const StreamReformInfo& reformInfo_;

	PacketCache audioCache_;
};

class AMTSimpleMuxder : public AMTObject {
public:
	AMTSimpleMuxder(
		AMTContext&ctx,
		const ConfigWrapper& setting)
		: AMTObject(ctx)
		, setting_(setting)
		, totalOutSize_(0)
	{ }

	void mux(VideoFormat videoFormat, int audioCount) {
		// Mux
		std::vector<tstring> audioFiles;
		for (int i = 0; i < audioCount; ++i) {
			audioFiles.push_back(setting_.getIntAudioFilePath(EncodeFileKey(), i));
		}
		tstring encVideoFile = setting_.getEncVideoFilePath(EncodeFileKey());
		tstring outFilePath = setting_.getOutFilePath(EncodeFileKey(), EncodeFileKey());
		auto args = makeMuxerArgs(
			FORMAT_MP4,
			setting_.getMuxerPath(), setting_.getTimelineEditorPath(), setting_.getMp4BoxPath(),
			encVideoFile, videoFormat, audioFiles, outFilePath,
			tstring(), tstring(), tstring(), std::pair<int, int>(),
			std::vector<tstring>(), std::vector<tstring>(), tstring());
		ctx.info("[Mux�J�n]");
		ctx.infoF("%s", args[0].first);

		{
			MySubProcess muxer(args[0].first);
			int ret = muxer.join();
			if (ret != 0) {
				THROWF(RuntimeException, "mux failed (muxer exit code: %d)", ret);
			}
		}

		{ // �o�̓T�C�Y�擾
			File outfile(setting_.getOutFilePath(EncodeFileKey(), EncodeFileKey()), _T("rb"));
			totalOutSize_ += outfile.size();
		}
	}

	int64_t getTotalOutSize() const {
		return totalOutSize_;
	}

private:
	class MySubProcess : public EventBaseSubProcess {
	public:
		MySubProcess(const tstring& args) : EventBaseSubProcess(args) { }
	protected:
		virtual void onOut(bool isErr, MemoryChunk mc) {
			// ����̓}���`�X���b�h�ŌĂ΂��̒���
			fwrite(mc.data, mc.length, 1, isErr ? stderr : stdout);
			fflush(isErr ? stderr : stdout);
		}
	};

	const ConfigWrapper& setting_;
	int64_t totalOutSize_;
};
