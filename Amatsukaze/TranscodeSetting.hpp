#pragma once

#include <string>
#include <sstream>
#include <iomanip>
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
	std::ostringstream ss;

	ss << "\"" << binpath << "\"";

	// y4m�w�b�_�ɂ���̂ŕK�v�Ȃ�
	//ss << " --fps " << fmt.frameRateNum << "/" << fmt.frameRateDenom;
	//ss << " --input-res " << fmt.width << "x" << fmt.height;
	//ss << " --sar " << fmt.sarWidth << ":" << fmt.sarHeight;

	if (fmt.colorPrimaries != AVCOL_PRI_UNSPECIFIED) {
		ss << " --colorprim " << av::getColorPrimStr(fmt.colorPrimaries);
	}
	if (fmt.transferCharacteristics != AVCOL_TRC_UNSPECIFIED) {
		ss << " --transfer " << av::getTransferCharacteristicsStr(fmt.transferCharacteristics);
	}
	if (fmt.colorSpace != AVCOL_TRC_UNSPECIFIED) {
		ss << " --colormatrix " << av::getColorSpaceStr(fmt.colorSpace);
	}

	// �C���^�[���[�X
	switch (encoder) {
	case ENCODER_X264:
	case ENCODER_QSVENC:
	case ENCODER_NVENC:
		ss << (fmt.progressive ? "" : " --tff");
		break;
	case ENCODER_X265:
		ss << (fmt.progressive ? " --no-interlace" : " --interlace tff");
		break;
	}

	ss << " " << options << " -o \"" << outpath << "\"";

	// ���͌`��
	switch (encoder) {
	case ENCODER_X264:
		ss << " --stitchable";
		ss << " --demuxer y4m -";
		break;
	case ENCODER_X265:
		ss << " --no-opt-qp-pps --no-opt-ref-list-length-pps";
		ss << " --y4m --input -";
		break;
	case ENCODER_QSVENC:
	case ENCODER_NVENC:
		ss << " --format raw --y4m -i -";
		break;
	}

	return ss.str();
}

static std::string makeMuxerArgs(
	const std::string& binpath,
	const std::string& inVideo,
	const VideoFormat& videoFormat,
	const std::vector<std::string>& inAudios,
	const std::string& outpath,
	const std::string& chapterpath)
{
	std::ostringstream ss;

	ss << "\"" << binpath << "\"";
	if (videoFormat.fixedFrameRate) {
		ss << " -i \"" << inVideo << "?fps="
			<< videoFormat.frameRateNum << "/"
			<< videoFormat.frameRateDenom << "\"";
	}
	else {
		ss << " -i \"" << inVideo << "\"";
	}
	for (const auto& inAudio : inAudios) {
		ss << " -i \"" << inAudio << "\"";
	}
	if (chapterpath.size() > 0) {
		ss << " --chapter \"" << chapterpath << "\"";
	}
	ss << " --optimize-pd";
	ss << " -o \"" << outpath << "\"";

	return ss.str();
}

static std::string makeTimelineEditorArgs(
	const std::string& binpath,
	const std::string& inpath,
	const std::string& outpath,
	const std::string& timecodepath,
	std::pair<int, int> timebase)
{
	std::ostringstream ss;
	ss << "\"" << binpath << "\"";
	ss << " --track 1";
	ss << " --timecode \"" << timecodepath << "\"";
	ss << " --media-timescale " << timebase.first;
	ss << " --media-timebase " << timebase.second;
	ss << " \"" << inpath << "\"";
	ss << " \"" << outpath << "\"";
	return ss.str();
}

static const char* cmOutMaskToString(int outmask) {
	switch (outmask)
	{
	case 1: return "�ʏ�";
	case 2: return "CM���J�b�g";
	case 3: return "�ʏ�+CM�J�b�g";
	case 4: return "CM�̂�";
	case 5: return "�ʏ�+CM";
	case 6: return "�{�҂�CM�𕪗�";
	case 7: return "�ʏ�+�{��+CM";
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
		std::ostringstream ss;
		ss << base << "/amt" << code;
		return ss.str();
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

class TranscoderSetting : public AMTObject
{
public:
	TranscoderSetting(
		AMTContext& ctx,
		std::string workDir,
		std::string mode,
		std::string modeArgs,
		std::string srcFilePath,
		std::string outVideoPath,
		std::string outInfoJsonPath,
		std::string filterScriptPath,
		std::string postFilterScriptPath,
		ENUM_ENCODER encoder,
		std::string encoderPath,
		std::string encoderOptions,
		std::string muxerPath,
		std::string timelineditorPath,
		bool twoPass,
		bool autoBitrate,
		bool chapter,
		BitrateSetting bitrate,
		double bitrateCM,
		int serviceId,
		DecoderSetting decoderSetting,
		std::vector<std::string> logoPath,
		bool errorOnNoLogo,
		std::string chapterExePath,
		std::string joinLogoScpPath,
		std::string joinLogoScpCmdPath,
    int cmoutmask,
		bool dumpStreamInfo,
		bool systemAvsPlugin)
		: AMTObject(ctx)
		, tmpDir(ctx, workDir)
		, mode(mode)
		, modeArgs(modeArgs)
		, srcFilePath(srcFilePath)
		, outVideoPath(outVideoPath)
		, outInfoJsonPath(outInfoJsonPath)
		, filterScriptPath(filterScriptPath)
		, postFilterScriptPath(postFilterScriptPath)
		, encoder(encoder)
		, encoderPath(encoderPath)
		, encoderOptions(encoderOptions)
		, muxerPath(muxerPath)
		, timelineditorPath(timelineditorPath)
		, twoPass(twoPass)
		, autoBitrate(autoBitrate)
		, chapter(chapter)
		, bitrate(bitrate)
		, bitrateCM(bitrateCM)
		, serviceId(serviceId)
		, decoderSetting(decoderSetting)
		, logoPath(logoPath)
		, errorOnNoLogo(errorOnNoLogo)
		, chapterExePath(chapterExePath)
		, joinLogoScpPath(joinLogoScpPath)
		, joinLogoScpCmdPath(joinLogoScpCmdPath)
    , cmoutmask(cmoutmask)
		, dumpStreamInfo(dumpStreamInfo)
		, systemAvsPlugin(systemAvsPlugin)
	{
		for (int cmtypei = 0; cmtypei < CMTYPE_MAX; ++cmtypei) {
			if (cmoutmask & (1 << cmtypei)) {
				cmtypes.push_back((CMType)cmtypei);
			}
		}
	}

	std::string getMode() const {
		return mode;
	}

	std::string getModeArgs() const {
		return modeArgs;
	}

	std::string getSrcFilePath() const {
		return srcFilePath;
	}

	std::string getOutInfoJsonPath() const {
		return outInfoJsonPath;
	}

	std::string getFilterScriptPath() const {
		return filterScriptPath;
	}

	std::string getPostFilterScriptPath() const {
		return postFilterScriptPath;
	}

	ENUM_ENCODER getEncoder() const {
		return encoder;
	}

	std::string getEncoderPath() const {
		return encoderPath;
	}

	std::string getMuxerPath() const {
		return muxerPath;
	}

	std::string getTimelineEditorPath() const {
		return timelineditorPath;
	}

	bool isTwoPass() const {
		return twoPass;
	}

	bool isAutoBitrate() const {
		return autoBitrate;
	}

	bool isChapterEnabled() const {
		return chapter;
	}

	BitrateSetting getBitrate() const {
		return bitrate;
	}

	double getBitrateCM() const {
		return bitrateCM;
	}

	int getServiceId() const {
		return serviceId;
	}

	DecoderSetting getDecoderSetting() const {
		return decoderSetting;
	}

	const std::vector<std::string>& getLogoPath() const {
		return logoPath;
	}

	bool getErrorOnNoLogo() const {
		return errorOnNoLogo;
	}

	std::string getChapterExePath() const {
		return chapterExePath;
	}

	std::string getJoinLogoScpPath() const {
		return joinLogoScpPath;
	}

	std::string getJoinLogoScpCmdPath() const {
		return joinLogoScpCmdPath;
	}

  const std::vector<CMType>& getCMTypes() const {
    return cmtypes;
  }

	bool isDumpStreamInfo() const {
		return dumpStreamInfo;
	}

	bool isSystemAvsPlugin() const {
		return systemAvsPlugin;
	}

	std::string getAudioFilePath() const
	{
		std::ostringstream ss;
		ss << tmpDir.path() << "/audio.dat";
		ctx.registerTmpFile(ss.str());
		return ss.str();
	}

	std::string getWaveFilePath() const
	{
		std::ostringstream ss;
		ss << tmpDir.path() << "/audio.wav";
		ctx.registerTmpFile(ss.str());
		return ss.str();
	}

	std::string getIntVideoFilePath(int index) const
	{
		std::ostringstream ss;
		ss << tmpDir.path() << "/i" << index << ".mpg";
		ctx.registerTmpFile(ss.str());
		return ss.str();
	}

	std::string getStreamInfoPath() const
	{
		return outVideoPath + "-streaminfo.dat";
	}

	std::string getEncVideoFilePath(int vindex, int index, CMType cmtype) const
	{
		std::ostringstream ss;
		ss << tmpDir.path() << "/v" << vindex << "-" << index << GetCMSuffix(cmtype) << ".raw";
		ctx.registerTmpFile(ss.str());
		return ss.str();
	}

	std::string getEncStatsFilePath(int vindex, int index, CMType cmtype) const
	{
		std::ostringstream ss;
		ss << tmpDir.path() << "/s" << vindex << "-" << index << GetCMSuffix(cmtype) << ".log";
		ctx.registerTmpFile(ss.str());
		// x264��.mbtree����������̂�
		ctx.registerTmpFile(ss.str() + ".mbtree");
		// x265��.cutree����������̂�
		ctx.registerTmpFile(ss.str() + ".cutree");
		return ss.str();
	}

	std::string getIntAudioFilePath(int vindex, int index, int aindex, CMType cmtype) const
	{
		std::ostringstream ss;
		ss << tmpDir.path() << "/a" << vindex << "-" << index << "-" << aindex << GetCMSuffix(cmtype) << ".aac";
		ctx.registerTmpFile(ss.str());
		return ss.str();
	}

	std::string getLogoTmpFilePath() const
	{
		std::ostringstream ss;
		ss << tmpDir.path() << "/logotmp.dat";
		ctx.registerTmpFile(ss.str());
		return ss.str();
	}

	std::string getTmpAMTSourcePath(int vindex) const
	{
		std::ostringstream ss;
		ss << tmpDir.path() << "/amts" << vindex << ".dat";
		ctx.registerTmpFile(ss.str());
		return ss.str();
	}

	std::string getTmpSourceAVSPath(int vindex) const
	{
		std::ostringstream ss;
		ss << tmpDir.path() << "/amts" << vindex << ".avs";
		ctx.registerTmpFile(ss.str());
		return ss.str();
	}

	std::string getTmpLogoFramePath(int vindex) const
	{
		std::ostringstream ss;
		ss << tmpDir.path() << "/logof" << vindex << ".txt";
		ctx.registerTmpFile(ss.str());
		return ss.str();
	}

	std::string getTmpChapterExePath(int vindex) const
	{
		std::ostringstream ss;
		ss << tmpDir.path() << "/chapter_exe" << vindex << ".txt";
		ctx.registerTmpFile(ss.str());
		return ss.str();
	}

	std::string getTmpChapterExeOutPath(int vindex) const
	{
		std::ostringstream ss;
		ss << tmpDir.path() << "/chapter_exe_o" << vindex << ".txt";
		ctx.registerTmpFile(ss.str());
		return ss.str();
	}

	std::string getTmpTrimAVSPath(int vindex) const
	{
		std::ostringstream ss;
		ss << tmpDir.path() << "/trim" << vindex << ".avs";
		ctx.registerTmpFile(ss.str());
		return ss.str();
	}

	std::string getTmpJlsPath(int vindex) const
	{
		std::ostringstream ss;
		ss << tmpDir.path() << "/jls" << vindex << ".txt";
		ctx.registerTmpFile(ss.str());
		return ss.str();
	}

	std::string getTmpChapterPath(int vindex, int index, CMType cmtype) const
	{
		std::ostringstream ss;
		ss << tmpDir.path() << "/chapter" << vindex << "-" << index << GetCMSuffix(cmtype) << ".txt";
		ctx.registerTmpFile(ss.str());
		return ss.str();
	}

	std::string getOutFilePath(int index, CMType cmtype) const
	{
		std::ostringstream ss;
		ss << outVideoPath;
		if (index != 0) {
			ss << "-" << index;
		}
    ss << GetCMSuffix(cmtype) << ".mp4";
		return ss.str();
	}

	std::string getOutSummaryPath() const
	{
		std::ostringstream ss;
		ss << outVideoPath;
		ss << ".txt";
		return ss.str();
	}

	std::string getOptions(
		VIDEO_STREAM_FORMAT srcFormat, double srcBitrate, bool pulldown,
		int pass, const std::vector<EncoderZone>& zones, int vindex, int index, CMType cmtype) const
	{
		std::ostringstream ss;
		ss << encoderOptions;
		if (autoBitrate) {
			double targetBitrate = bitrate.getTargetBitrate(srcFormat, srcBitrate);
      if (cmtype == CMTYPE_CM) {
        targetBitrate *= bitrateCM;
      }
			if (encoder == ENCODER_QSVENC) {
				ss << " --la " << (int)targetBitrate;
				ss << " --maxbitrate " << (int)(targetBitrate * 2);
			}
			else if (encoder == ENCODER_NVENC) {
				ss << " --vbrhq " << (int)targetBitrate;
				ss << " --maxbitrate " << (int)(targetBitrate * 2);
			}
			else {
				ss << " --bitrate " << (int)targetBitrate;
				ss << " --vbv-maxrate " << (int)(targetBitrate * 2);
				ss << " --vbv-bufsize " << (int)(targetBitrate * 2);
			}
		}
		if (pass >= 0) {
			ss << " --pass " << pass;
			ss << " --stats \"" << getEncStatsFilePath(vindex, index, cmtype) << "\"";
		}
		if (zones.size() && bitrateCM != 1.0 && encoder != ENCODER_QSVENC && encoder != ENCODER_NVENC) {
			ss << " --zones ";
			ss << std::setprecision(3);
			for (int i = 0; i < (int)zones.size(); ++i) {
				auto zone = zones[i];
				if (i > 0) ss << "/";
				ss << zone.startFrame << "," << zone.endFrame << ",b=" << bitrateCM;
			}
		}
		return ss.str();
	}

	void dump() const {
		ctx.info("[�ݒ�]");
		if (mode != "ts") {
			ctx.info("Mode: %s", mode.c_str());
		}
		ctx.info("����: %s", srcFilePath.c_str());
		ctx.info("�o��: %s", outVideoPath.c_str());
		ctx.info("�ꎞ�t�H���_: %s", tmpDir.path().c_str());
		ctx.info("�G���R�[�_: %s (%s)", encoderPath.c_str(), encoderToString(encoder));
		ctx.info("�G���R�[�_�I�v�V����: %s", encoderOptions.c_str());
		if (autoBitrate) {
			ctx.info("�����r�b�g���[�g: �L�� (%g:%g:%g)", bitrate.a, bitrate.b, bitrate.h264);
		}
		else {
			ctx.info("�����r�b�g���[�g: ����");
		}
		ctx.info("�G���R�[�h/�o��: %s/%s",
			twoPass ? "2�p�X" : "1�p�X",
			cmOutMaskToString(cmoutmask));
    ctx.info("�`���v�^�[���: %s%s",
			chapter ? "�L��" : "����",
			(chapter && errorOnNoLogo) ? "�i���S�K�{�j" : "");
    if (chapter) {
      for (int i = 0; i < (int)logoPath.size(); ++i) {
        ctx.info("logo%d: %s", (i + 1), logoPath[i].c_str());
      }
    }
		if (serviceId > 0) {
			ctx.info("ServiceId: %d", serviceId);
		}
		else {
			ctx.info("ServiceId: �w��Ȃ�");
		}
		ctx.info("�f�R�[�_: MPEG2:%s H264:%s",
			decoderToString(decoderSetting.mpeg2),
			decoderToString(decoderSetting.h264));
	}

private:
	TempDirectory tmpDir;

	std::string mode;
	std::string modeArgs; // �e�X�g�p
	// ���̓t�@�C���p�X�i�g���q���܂ށj
	std::string srcFilePath;
	// �o�̓t�@�C���p�X�i�g���q�������j
	std::string outVideoPath;
	// ���ʏ��JSON�o�̓p�X
	std::string outInfoJsonPath;
	std::string filterScriptPath;
	std::string postFilterScriptPath;
	// �G���R�[�_�ݒ�
	ENUM_ENCODER encoder;
	std::string encoderPath;
	std::string encoderOptions;
	std::string muxerPath;
	std::string timelineditorPath;
	bool twoPass;
	bool autoBitrate;
	bool chapter;
	BitrateSetting bitrate;
	double bitrateCM;
	int serviceId;
	DecoderSetting decoderSetting;
	// CM��͗p�ݒ�
	std::vector<std::string> logoPath;
	bool errorOnNoLogo;
	std::string chapterExePath;
	std::string joinLogoScpPath;
	std::string joinLogoScpCmdPath;
  int cmoutmask;
	std::vector<CMType> cmtypes;
	// �f�o�b�O�p�ݒ�
	bool dumpStreamInfo;
	bool systemAvsPlugin;

	const char* decoderToString(DECODER_TYPE decoder) const {
		switch (decoder) {
		case DECODER_QSV: return "QSV";
		case DECODER_CUVID: return "CUVID";
		}
		return "default";
	}
};

