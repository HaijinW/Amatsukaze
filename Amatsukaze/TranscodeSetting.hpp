/**
* Amtasukaze Transcode Setting
* Copyright (c) 2017-2018 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include <string>
#include <direct.h>

#include "StreamUtils.hpp"

// �J���[�X�y�[�X��`���g������
#include "libavutil/pixfmt.h"

struct EncoderZone {
	int startFrame;
	int endFrame;
};

namespace av {

// �J���[�X�y�[�X3�Z�b�g
// x265�͐��l���̂܂܂ł�OK�����Ax264��help���������string�łȂ����
// �Ȃ�Ȃ��悤�Ȃ̂ŕϊ����`
// �Ƃ肠����ARIB STD-B32 v3.7�ɏ����Ă���̂���

// 3���F
static const char* getColorPrimStr(int color_prim) {
	switch (color_prim) {
	case AVCOL_PRI_BT709: return "bt709";
	case AVCOL_PRI_BT2020: return "bt2020";
	default:
		THROWF(FormatException,
			"Unsupported color primaries (%d)", color_prim);
	}
	return NULL;
}

// �K���}
static const char* getTransferCharacteristicsStr(int transfer_characteritics) {
	switch (transfer_characteritics) {
	case AVCOL_TRC_BT709: return "bt709";
	case AVCOL_TRC_IEC61966_2_4: return "iec61966-2-4";
	case AVCOL_TRC_BT2020_10: return "bt2020-10";
	case AVCOL_TRC_SMPTEST2084: return "smpte-st-2084";
	case AVCOL_TRC_ARIB_STD_B67: return "arib-std-b67";
	default:
		THROWF(FormatException,
			"Unsupported color transfer characteritics (%d)", transfer_characteritics);
	}
	return NULL;
}

// �ϊ��W��
static const char* getColorSpaceStr(int color_space) {
	switch (color_space) {
	case AVCOL_SPC_BT709: return "bt709";
	case AVCOL_SPC_BT2020_NCL: return "bt2020nc";
	default:
		THROWF(FormatException,
			"Unsupported color color space (%d)", color_space);
	}
	return NULL;
}

} // namespace av {

enum ENUM_ENCODER {
	ENCODER_X264,
	ENCODER_X265,
	ENCODER_QSVENC,
	ENCODER_NVENC,
};

enum ENUM_FORMAT {
	FORMAT_MP4,
	FORMAT_MKV
};

struct BitrateSetting {
	double a, b;
	double h264;
	double h265;

	double getTargetBitrate(VIDEO_STREAM_FORMAT format, double srcBitrate) const {
		double base = a * srcBitrate + b;
		if (format == VS_H264) {
			return base * h264;
		}
		else if (format == VS_H265) {
			return base * h265;
		}
		return base;
	}
};

static const char* encoderToString(ENUM_ENCODER encoder) {
	switch (encoder) {
	case ENCODER_X264: return "x264";
	case ENCODER_X265: return "x265";
	case ENCODER_QSVENC: return "QSVEnc";
	case ENCODER_NVENC: return "NVEnc";
	}
	return "Unknown";
}

static std::string makeEncoderArgs(
	ENUM_ENCODER encoder,
	const std::string& binpath,
	const std::string& options,
	const VideoFormat& fmt,
	const std::string& outpath)
{
	StringBuilder sb;

	sb.append("\"%s\"", binpath);

	// y4m�w�b�_�ɂ���̂ŕK�v�Ȃ�
	//ss << " --fps " << fmt.frameRateNum << "/" << fmt.frameRateDenom;
	//ss << " --input-res " << fmt.width << "x" << fmt.height;
	//ss << " --sar " << fmt.sarWidth << ":" << fmt.sarHeight;

	if (fmt.colorPrimaries != AVCOL_PRI_UNSPECIFIED) {
		sb.append(" --colorprim %s", av::getColorPrimStr(fmt.colorPrimaries));
	}
	if (fmt.transferCharacteristics != AVCOL_TRC_UNSPECIFIED) {
		sb.append(" --transfer %s", av::getTransferCharacteristicsStr(fmt.transferCharacteristics));
	}
	if (fmt.colorSpace != AVCOL_TRC_UNSPECIFIED) {
		sb.append(" --colormatrix %s", av::getColorSpaceStr(fmt.colorSpace));
	}

	// �C���^�[���[�X
	switch (encoder) {
	case ENCODER_X264:
	case ENCODER_QSVENC:
	case ENCODER_NVENC:
		sb.append(fmt.progressive ? "" : " --tff");
		break;
	case ENCODER_X265:
		sb.append(fmt.progressive ? " --no-interlace" : " --interlace tff");
		break;
	}

	sb.append(" %s -o \"%s\"", options, outpath);

	// ���͌`��
	switch (encoder) {
	case ENCODER_X264:
		sb.append(" --stitchable")
			.append(" --demuxer y4m -");
		break;
	case ENCODER_X265:
		sb.append(" --no-opt-qp-pps --no-opt-ref-list-length-pps")
			.append(" --y4m --input -");
		break;
	case ENCODER_QSVENC:
	case ENCODER_NVENC:
		sb.append(" --format raw --y4m -i -");
		break;
	}

	return sb.str();
}

static std::vector<std::string> makeMuxerArgs(
	ENUM_FORMAT format,
	const std::string& binpath,
	const std::string& timelineeditorpath,
	const std::string& mp4boxpath,
	const std::string& inVideo,
	const VideoFormat& videoFormat,
	const std::vector<std::string>& inAudios,
	const std::string& outpath,
	const std::string& tmpoutpath,
	const std::string& chapterpath,
	const std::string& timecodepath,
	std::pair<int, int> timebase,
	const std::vector<std::string>& inSubs,
	const std::vector<std::string>& subsTitles)
{
	std::vector<std::string> ret;

	StringBuilder sb;
	sb.append("\"%s\"", binpath);

	if (format == FORMAT_MP4) {

		// �܂���muxer�ŉf���A�����A�`���v�^�[��mux
		if (videoFormat.fixedFrameRate) {
			sb.append(" -i \"%s?fps=%d/%d\"", inVideo,
				videoFormat.frameRateNum, videoFormat.frameRateDenom);
		}
		else {
			sb.append(" -i \"%s\"", inVideo);
		}
		for (const auto& inAudio : inAudios) {
			sb.append(" -i \"%s\"", inAudio);
		}
		if (chapterpath.size() > 0) {
			sb.append(" --chapter \"%s\"", chapterpath);
		}
		sb.append(" --optimize-pd");

		std::string dst = (timecodepath.size() > 0) ? tmpoutpath : outpath;
		sb.append(" -o \"%s\"", dst);

		ret.push_back(sb.str());
		sb.clear();

		if (timecodepath.size() > 0) {
			// �K�v�Ȃ�timelineeditor��timecode�𖄂ߍ���
			sb.append("\"%s\"", timelineeditorpath)
				.append(" --track 1")
				.append(" --timecode \"%s\"", timecodepath)
				.append(" --media-timescale %d", timebase.first)
				.append(" --media-timebase %d", timebase.second)
				.append(" \"%s\"", dst)
				.append(" \"%s\"", outpath);
			ret.push_back(sb.str());
			sb.clear();
		}

		if (inSubs.size() > 0) {
			// ����Ύ����𖄂ߍ���
			sb.append("\"%s\"", mp4boxpath);
			for (int i = 0; i < (int)inSubs.size(); ++i) {
				if (subsTitles[i] == "SRT") { // mp4��SRT�̂�
					sb.append(" -add \"%s#:name=%s\"", inSubs[i], subsTitles[i]);
				}
			}
			sb.append(" \"%s\"", outpath);
			ret.push_back(sb.str());
			sb.clear();
		}
	}
	else { // mkv

		if (chapterpath.size() > 0) {
			sb.append(" --chapters \"%s\"", chapterpath);
		}

		sb.append(" -o \"%s\"", outpath);

		if (timecodepath.size()) {
			sb.append(" --timestamps \"0:%s\"", timecodepath);
		}
		sb.append(" \"%s\"", inVideo);

		for (const auto& inAudio : inAudios) {
			sb.append(" \"%s\"", inAudio);
		}
		for (int i = 0; i < (int)inSubs.size(); ++i) {
			sb.append(" --track-name \"0:%s\" \"%s\"", subsTitles[i], inSubs[i]);
		}

		ret.push_back(sb.str());
		sb.clear();
	}

	return ret;
}

static std::string makeTimelineEditorArgs(
	const std::string& binpath,
	const std::string& inpath,
	const std::string& outpath,
	const std::string& timecodepath)
{
	StringBuilder sb;
	sb.append("\"%s\"", binpath)
		.append(" --track 1")
		.append(" --timecode \"%s\"", timecodepath)
		.append(" \"%s\"", inpath)
		.append(" \"%s\"", outpath);
	return sb.str();
}

static const char* cmOutMaskToString(int outmask) {
	switch (outmask)
	{
	case 1: return "�ʏ�";
	case 2: return "CM���J�b�g";
	case 3: return "�ʏ�o�͂�CM�J�b�g�o��";
	case 4: return "CM�̂�";
	case 5: return "�ʏ�o�͂�CM�o��";
	case 6: return "�{�҂�CM�𕪗�";
	case 7: return "�ʏ�,�{��,CM�S�o��";
	}
	return "�s��";
}

inline bool ends_with(std::string const & value, std::string const & ending)
{
	if (ending.size() > value.size()) return false;
	return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

enum AMT_CLI_MODE {
	AMT_CLI_TS,
	AMT_CLI_GENERIC,
};

class TempDirectory : AMTObject, NonCopyable
{
public:
	TempDirectory(AMTContext& ctx, const std::string& tmpdir)
		: AMTObject(ctx)
	{
		if (tmpdir.size() == 0) {
			// �w�肪�Ȃ���΍��Ȃ�
			return;
		}
		for (int code = (int)time(NULL) & 0xFFFFFF; code > 0; ++code) {
			auto path = genPath(tmpdir, code);
			if (_mkdir(path.c_str()) == 0) {
				path_ = path;
				break;
			}
			if (errno != EEXIST) {
				break;
			}
		}
		if (path_.size() == 0) {
			THROW(IOException, "�ꎞ�f�B���N�g���쐬���s");
		}
		
		std::string abolutePath;
		int sz = GetFullPathNameA(path_.c_str(), 0, 0, 0);
		abolutePath.resize(sz);
		GetFullPathNameA(path_.c_str(), sz, &abolutePath[0], 0);
		abolutePath.resize(sz - 1);
		path_ = abolutePath;
	}
	~TempDirectory() {
		if (path_.size() == 0) {
			return;
		}
		// �ꎞ�t�@�C�����폜
		ctx.clearTmpFiles();
		// �f�B���N�g���폜
		if (_rmdir(path_.c_str()) != 0) {
			ctx.warn("�ꎞ�f�B���N�g���폜�Ɏ��s: ", path_.c_str());
		}
	}

	std::string path() const {
		if (path_.size() == 0) {
			THROW(RuntimeException, "�ꎞ�t�H���_�̎w�肪����܂���");
		}
		return path_;
	}

private:
	std::string path_;

	std::string genPath(const std::string& base, int code)
	{
		return StringFormat("%s/amt%d", base, code);
	}
};

static const char* GetCMSuffix(CMType cmtype) {
	switch (cmtype) {
	case CMTYPE_CM: return "-cm";
	case CMTYPE_NONCM: return "-main";
	case CMTYPE_BOTH: return "";
	}
	return "";
}

struct Config {
	// �ꎞ�t�H���_
	std::string workDir;
	std::string mode;
	std::string modeArgs; // �e�X�g�p
	// ���̓t�@�C���p�X�i�g���q���܂ށj
	std::string srcFilePath;
	// �o�̓t�@�C���p�X�i�g���q�������j
	std::string outVideoPath;
	// ���ʏ��JSON�o�̓p�X
	std::string outInfoJsonPath;
	// DRCS�}�b�s���O�t�@�C���p�X
	std::string drcsMapPath;
	std::string drcsOutPath;
	// �t�B���^�p�X
	std::string filterScriptPath;
	std::string postFilterScriptPath;
	// �G���R�[�_�ݒ�
	ENUM_ENCODER encoder;
	std::string encoderPath;
	std::string encoderOptions;
	std::string muxerPath;
	std::string timelineditorPath;
	std::string mp4boxPath;
	ENUM_FORMAT format;
	bool splitSub;
	bool twoPass;
	bool autoBitrate;
	bool chapter;
	bool subtitles;
	BitrateSetting bitrate;
	double bitrateCM;
	int serviceId;
	DecoderSetting decoderSetting;
	// CM��͗p�ݒ�
	std::vector<std::string> logoPath;
	bool ignoreNoLogo;
	bool ignoreNoDrcsMap;
	std::string chapterExePath;
	std::string joinLogoScpPath;
	std::string joinLogoScpCmdPath;
	std::string joinLogoScpOptions;
	int cmoutmask;
	// �f�o�b�O�p�ݒ�
	bool dumpStreamInfo;
	bool systemAvsPlugin;
};

class ConfigWrapper : public AMTObject
{
public:
	ConfigWrapper(
		AMTContext& ctx,
		const Config& conf)
		: AMTObject(ctx)
		, conf(conf)
		, tmpDir(ctx, conf.workDir)
	{
		for (int cmtypei = 0; cmtypei < CMTYPE_MAX; ++cmtypei) {
			if (conf.cmoutmask & (1 << cmtypei)) {
				cmtypes.push_back((CMType)cmtypei);
			}
		}
	}

	std::string getMode() const {
		return conf.mode;
	}

	std::string getModeArgs() const {
		return conf.modeArgs;
	}

	std::string getSrcFilePath() const {
		return conf.srcFilePath;
	}

	std::string getOutInfoJsonPath() const {
		return conf.outInfoJsonPath;
	}

	std::string getFilterScriptPath() const {
		return conf.filterScriptPath;
	}

	std::string getPostFilterScriptPath() const {
		return conf.postFilterScriptPath;
	}

	ENUM_ENCODER getEncoder() const {
		return conf.encoder;
	}

	std::string getEncoderPath() const {
		return conf.encoderPath;
	}

	std::string getEncoderOptions() const {
		return conf.encoderOptions;
	}

	ENUM_FORMAT getFormat() const {
		return conf.format;
	}

	std::string getMuxerPath() const {
		return conf.muxerPath;
	}

	std::string getTimelineEditorPath() const {
		return conf.timelineditorPath;
	}

	std::string getMp4BoxPath() const {
		return conf.mp4boxPath;
	}

	bool isSplitSub() const {
		return conf.splitSub;
	}

	bool isTwoPass() const {
		return conf.twoPass;
	}

	bool isAutoBitrate() const {
		return conf.autoBitrate;
	}

	bool isChapterEnabled() const {
		return conf.chapter;
	}

	bool isSubtitlesEnabled() const {
		return conf.subtitles;
	}

	BitrateSetting getBitrate() const {
		return conf.bitrate;
	}

	double getBitrateCM() const {
		return conf.bitrateCM;
	}

	int getServiceId() const {
		return conf.serviceId;
	}

	DecoderSetting getDecoderSetting() const {
		return conf.decoderSetting;
	}

	const std::vector<std::string>& getLogoPath() const {
		return conf.logoPath;
	}

	bool isIgnoreNoLogo() const {
		return conf.ignoreNoLogo;
	}

	bool isIgnoreNoDrcsMap() const {
		return conf.ignoreNoDrcsMap;
	}

	std::string getChapterExePath() const {
		return conf.chapterExePath;
	}

	std::string getJoinLogoScpPath() const {
		return conf.joinLogoScpPath;
	}

	std::string getJoinLogoScpCmdPath() const {
		return conf.joinLogoScpCmdPath;
	}

	std::string getJoinLogoScpOptions() const {
		return conf.joinLogoScpOptions;
	}

	const std::vector<CMType>& getCMTypes() const {
		return cmtypes;
	}

	bool isDumpStreamInfo() const {
		return conf.dumpStreamInfo;
	}

	bool isSystemAvsPlugin() const {
		return conf.systemAvsPlugin;
	}

	std::string getAudioFilePath() const {
		return regtmp(StringFormat("%s/audio.dat", tmpDir.path()));
	}

	std::string getWaveFilePath() const {
		return regtmp(StringFormat("%s/audio.wav", tmpDir.path()));
	}

	std::string getIntVideoFilePath(int index) const {
		return regtmp(StringFormat("%s/i%d.mpg", tmpDir.path(), index));
	}

	std::string getStreamInfoPath() const {
		return conf.outVideoPath + "-streaminfo.dat";
	}

	std::string getEncVideoFilePath(int vindex, int index, CMType cmtype) const {
		return regtmp(StringFormat("%s/v%d-%d%s.raw", tmpDir.path(), vindex, index, GetCMSuffix(cmtype)));
	}

	std::string getTimecodeFilePath(int vindex, int index, CMType cmtype) const {
		return regtmp(StringFormat("%s/v%d-%d%s.timecode.txt", tmpDir.path(), vindex, index, GetCMSuffix(cmtype)));
	}

	std::string getEncStatsFilePath(int vindex, int index, CMType cmtype) const
	{
		auto str = StringFormat("%s/s%d-%d%s.log", tmpDir.path(), vindex, index, GetCMSuffix(cmtype));
		ctx.registerTmpFile(str);
		// x264��.mbtree����������̂�
		ctx.registerTmpFile(str + ".mbtree");
		// x265��.cutree����������̂�
		ctx.registerTmpFile(str + ".cutree");
		return str;
	}

	std::string getIntAudioFilePath(int vindex, int index, int aindex, CMType cmtype) const {
		return regtmp(StringFormat("%s/a%d-%d-%d%s.aac",
			tmpDir.path(), vindex, index, aindex, GetCMSuffix(cmtype)));
	}

	std::string getTmpASSFilePath(int vindex, int index, int langindex, CMType cmtype) const {
		return regtmp(StringFormat("%s/c%d-%d-%d%s.ass",
			tmpDir.path(), vindex, index, langindex, GetCMSuffix(cmtype)));
	}

	std::string getTmpSRTFilePath(int vindex, int index, int langindex, CMType cmtype) const {
		return regtmp(StringFormat("%s/c%d-%d-%d%s.srt",
			tmpDir.path(), vindex, index, langindex, GetCMSuffix(cmtype)));
	}

	std::string getLogoTmpFilePath() const {
		return regtmp(StringFormat("%s/logotmp.dat", tmpDir.path()));
	}

	std::string getTmpAMTSourcePath(int vindex) const {
		return regtmp(StringFormat("%s/amts%d.dat", tmpDir.path(), vindex));
	}

	std::string getTmpSourceAVSPath(int vindex) const {
		return regtmp(StringFormat("%s/amts%d.avs", tmpDir.path(), vindex));
	}

	std::string getTmpLogoFramePath(int vindex) const {
		return regtmp(StringFormat("%s/logof%d.txt", tmpDir.path(), vindex));
	}

	std::string getTmpChapterExePath(int vindex) const {
		return regtmp(StringFormat("%s/chapter_exe%d.txt", tmpDir.path(), vindex));
	}

	std::string getTmpChapterExeOutPath(int vindex) const {
		return regtmp(StringFormat("%s/chapter_exe_o%d.txt", tmpDir.path(), vindex));
	}

	std::string getTmpTrimAVSPath(int vindex) const {
		return regtmp(StringFormat("%s/trim%d.avs", tmpDir.path(), vindex));
	}

	std::string getTmpJlsPath(int vindex) const {
		return regtmp(StringFormat("%s/jls%d.txt", tmpDir.path(), vindex));
	}

	std::string getTmpChapterPath(int vindex, int index, CMType cmtype) const {
		return regtmp(StringFormat("%s/chapter%d-%d%s.txt",
			tmpDir.path(), vindex, index, GetCMSuffix(cmtype)));
	}

	std::string getVfrTmpFilePath(int vindex, int index, CMType cmtype) const {
		return regtmp(StringFormat("%s/t%d-%d%s.mp4", tmpDir.path(), vindex, index, GetCMSuffix(cmtype)));
	}

	const char* getOutputExtention() const {
		switch (conf.format) {
		case FORMAT_MP4: return "mp4";
		case FORMAT_MKV: return "mkv";
		}
		return "amatsukze";
	}

	std::string getOutFilePath(int index, CMType cmtype) const {
		StringBuilder sb;
		sb.append("%s", conf.outVideoPath);
		if (index != 0) {
			sb.append("-%d", index);
		}
		sb.append("%s.%s", GetCMSuffix(cmtype), getOutputExtention());
		return sb.str();
	}

	std::string getOutSummaryPath() const {
		return StringFormat("%s.txt", conf.outVideoPath);
	}

	std::string getDRCSMapPath() const {
		return conf.drcsMapPath;
	}

	std::string getDRCSOutPath(const std::string& md5) const {
		return StringFormat("%s\\%s.bmp", conf.drcsOutPath, md5);
	}

	std::string getOptions(
		VIDEO_STREAM_FORMAT srcFormat, double srcBitrate, bool pulldown,
		int pass, const std::vector<EncoderZone>& zones, int vindex, int index, CMType cmtype) const
	{
		StringBuilder sb;
		sb.append("%s", conf.encoderOptions);
		if (conf.autoBitrate) {
			double targetBitrate = conf.bitrate.getTargetBitrate(srcFormat, srcBitrate);
			if (cmtype == CMTYPE_CM) {
				targetBitrate *= conf.bitrateCM;
			}
			if (conf.encoder == ENCODER_QSVENC) {
				sb.append(" --la %d --maxbitrate %d", (int)targetBitrate, (int)(targetBitrate * 2));
			}
			else if (conf.encoder == ENCODER_NVENC) {
				sb.append(" --vbrhq %d --maxbitrate %d", (int)targetBitrate, (int)(targetBitrate * 2));
			}
			else {
				sb.append(" --bitrate %d --vbv-maxrate %d --vbv-bufsize %d", 
					(int)targetBitrate, (int)(targetBitrate * 2), (int)(targetBitrate * 2));
			}
		}
		if (pass >= 0) {
			sb.append(" --pass %d --stats \"%s\"",
				pass, getEncStatsFilePath(vindex, index, cmtype));
		}
		if (zones.size() && conf.bitrateCM != 1.0 && conf.encoder != ENCODER_QSVENC && conf.encoder != ENCODER_NVENC) {
			sb.append(" --zones ");
			for (int i = 0; i < (int)zones.size(); ++i) {
				auto zone = zones[i];
				sb.append("%s%d,%d,b=%3g", (i > 0) ? "/" : "", zone.startFrame, zone.endFrame, conf.bitrateCM);
			}
		}
		return sb.str();
	}

	void dump() const {
		ctx.info("[�ݒ�]");
		if (conf.mode != "ts") {
			ctx.info("Mode: %s", conf.mode.c_str());
		}
		ctx.info("����: %s", conf.srcFilePath.c_str());
		ctx.info("�o��: %s", conf.outVideoPath.c_str());
		ctx.info("�ꎞ�t�H���_: %s", tmpDir.path().c_str());
		ctx.info("�o�̓t�H�[�}�b�g: %s", formatToString(conf.format));
		ctx.info("�G���R�[�_: %s (%s)", conf.encoderPath.c_str(), encoderToString(conf.encoder));
		ctx.info("�G���R�[�_�I�v�V����: %s", conf.encoderOptions.c_str());
		if (conf.autoBitrate) {
			ctx.info("�����r�b�g���[�g: �L�� (%g:%g:%g)", 
				conf.bitrate.a, conf.bitrate.b, conf.bitrate.h264);
		}
		else {
			ctx.info("�����r�b�g���[�g: ����");
		}
		ctx.info("�G���R�[�h/�o��: %s/%s",
			conf.twoPass ? "2�p�X" : "1�p�X",
			cmOutMaskToString(conf.cmoutmask));
		ctx.info("�`���v�^�[���: %s%s",
			conf.chapter ? "�L��" : "����",
			(conf.chapter && conf.ignoreNoLogo) ? "" : "�i���S�K�{�j");
		if (conf.chapter) {
			for (int i = 0; i < (int)conf.logoPath.size(); ++i) {
				ctx.info("logo%d: %s", (i + 1), conf.logoPath[i].c_str());
			}
		}
		ctx.info("����: %s", conf.subtitles ? "�L��" : "����");
		if (conf.subtitles) {
			ctx.info("DRCS�}�b�s���O: %s", conf.drcsMapPath.c_str());
		}
		if (conf.serviceId > 0) {
			ctx.info("�T�[�r�XID: %d", conf.serviceId);
		}
		else {
			ctx.info("�T�[�r�XID: �w��Ȃ�");
		}
		ctx.info("�f�R�[�_: MPEG2:%s H264:%s",
			decoderToString(conf.decoderSetting.mpeg2),
			decoderToString(conf.decoderSetting.h264));
	}

private:
	Config conf;
	TempDirectory tmpDir;
	std::vector<CMType> cmtypes;

	const char* decoderToString(DECODER_TYPE decoder) const {
		switch (decoder) {
		case DECODER_QSV: return "QSV";
		case DECODER_CUVID: return "CUVID";
		}
		return "default";
	}

	const char* formatToString(ENUM_FORMAT fmt) const {
		switch (fmt) {
		case FORMAT_MP4: return "MP4";
		case FORMAT_MKV: return "Matroska";
		}
		return "unknown";
	}

	std::string regtmp(std::string str) const {
		ctx.registerTmpFile(str);
		return str;
	}
};

