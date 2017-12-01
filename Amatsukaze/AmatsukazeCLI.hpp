/**
* Amtasukaze Command Line Interface
* Copyright (c) 2017 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include <time.h>

#include "TranscodeManager.hpp"
#include "AmatsukazeTestImpl.hpp"

// MSVC�̃}���`�o�C�g��Unicode�łȂ��̂ŕ����񑀍�ɓK���Ȃ��̂�wchar_t�ŕ����񑀍������
#ifdef _MSC_VER
namespace std { typedef wstring tstring; }
typedef wchar_t tchar;
#define stscanf swscanf_s
#define PRITSTR "ls"
#define _T(s) L ## s
#else
namespace std { typedef string tstring; }
typedef char tchar;
#define stscanf sscanf
#define PRITSTR "s"
#define _T(s) s
#endif

static std::string to_string(std::wstring str) {
	if (str.size() == 0) {
		return std::string();
	}
	int dstlen = WideCharToMultiByte(
		CP_ACP, 0, str.c_str(), (int)str.size(), NULL, 0, NULL, NULL);
	std::vector<char> ret(dstlen);
	WideCharToMultiByte(CP_ACP, 0,
		str.c_str(), (int)str.size(), ret.data(), (int)ret.size(), NULL, NULL);
	return std::string(ret.begin(), ret.end());
}

static std::string to_string(std::string str) {
	return str;
}

static void printCopyright() {
	PRINTF(
		"Amatsukaze - Automated MPEG2-TS Transcode\n"
		"Built on %s %s\n"
		"Copyright (c) 2017 Nekopanda\n", __DATE__, __TIME__);
}

static void printHelp(const tchar* bin) {
	printf(
		"%" PRITSTR " <�I�v�V����> -i <input.ts> -o <output.mp4>\n"
		"�I�v�V���� []�̓f�t�H���g�l \n"
		"  -i|--input  <�p�X>  ���̓t�@�C���p�X\n"
		"  -o|--output <�p�X>  �o�̓t�@�C���p�X\n"
		"  -s|--serviceid <���l> ��������T�[�r�XID���w��[]\n"
		"  -w|--work   <�p�X>  �ꎞ�t�@�C���p�X[./]\n"
		"  -et|--encoder-type <�^�C�v>  �g�p�G���R�[�_�^�C�v[x264]\n"
		"                      �Ή��G���R�[�_: x264,x265,QSVEnc,NVEnc\n"
		"  -e|--encoder <�p�X> �G���R�[�_�p�X[x264.exe]\n"
		"  -eo|--encoder-opotion <�I�v�V����> �G���R�[�_�֓n���I�v�V����[]\n"
		"                      ���̓t�@�C���̉𑜓x�A�A�X�y�N�g��A�C���^���[�X�t���O�A\n"
		"                      �t���[�����[�g�A�J���[�}�g���N�X���͎����Œǉ������̂ŕs�v\n"
		"  -b|--bitrate a:b:f  �r�b�g���[�g�v�Z�� �f���r�b�g���[�gkbps = f*(a*s+b)\n"
		"                      s�͓��͉f���r�b�g���[�g�Af�͓��͂�H264�̏ꍇ�͓��͂��ꂽf�����A\n"
		"                      ���͂�MPEG2�̏ꍇ��f=1�Ƃ���\n"
		"                      �w�肪�Ȃ��ꍇ�̓r�b�g���[�g�I�v�V������ǉ����Ȃ�\n"
		"  -bcm|--bitrate-cm <float>   CM���肳�ꂽ�Ƃ���̃r�b�g���[�g�{��\n"
		"  --2pass             2pass�G���R�[�h\n"
		"  --splitsub          ���C���ȊO�̃t�H�[�}�b�g�͌������Ȃ�\n"
		"  -fmt|--format <�t�H�[�}�b�g> �o�̓t�H�[�}�b�g[mp4]\n"
		"                      �Ή��G���R�[�_: mp4,mkv\n"
		"  -m|--muxer  <�p�X>  L-SMASH��muxer�܂���mkvmerge�ւ̃p�X[muxer.exe]\n"
		"  -t|--timelineeditor  <�p�X>  timelineeditor�ւ̃p�X�iMP4��VFR�o�͂���ꍇ�ɕK�v�j[timelineeditor.exe]\n"
		"  --mp4box <�p�X>     mp4box�ւ̃p�X�iMP4�Ŏ�����������ꍇ�ɕK�v�j[mp4box.exe]\n"
		"  -f|--filter <�p�X>  �t�B���^Avisynth�X�N���v�g�ւ̃p�X[]\n"
		"  -pf|--postfilter <�p�X>  �|�X�g�t�B���^Avisynth�X�N���v�g�ւ̃p�X[]\n"
		"  --mpeg2decoder <�f�R�[�_>  MPEG2�p�f�R�[�_[default]\n"
		"                      �g�p�\�f�R�[�_: default,QSV,CUVID\n"
		"  --h264decoder <�f�R�[�_>  H264�p�f�R�[�_[default]\n"
		"                      �g�p�\�f�R�[�_: default,QSV,CUVID\n"
		"  --chapter           �`���v�^�[�ECM��͂��s��\n"
		"  --subtitles         ��������������\n"
		"  --logo <�p�X>       ���S�t�@�C�����w��i�����ł��w��\�j\n"
		"  --drcs <�p�X>       DRCS�}�b�s���O�t�@�C���p�X\n"
		"  --ignore-no-drcsmap �}�b�s���O�ɂȂ�DRCS�O���������Ă������𑱍s����\n"
		"  --ignore-no-logo    ���S��������Ȃ��Ă������𑱍s����\n"
		"  --chapter-exe <�p�X> chapter_exe.exe�ւ̃p�X\n"
		"  --jls <�p�X>         join_logo_scp.exe�ւ̃p�X\n"
		"  --jls-cmd <�p�X>    join_logo_scp�̃R�}���h�t�@�C���ւ̃p�X\n"
		"  -om|--cmoutmask <���l> �o�̓}�X�N[1]\n"
		"                      1 : �ʏ�\n"
		"                      2 : CM���J�b�g\n"
		"                      4 : CM�̂ݏo��\n"
		"                      OR���� ��) 6: �{�҂�CM�𕪗�\n"
		"  -j|--json   <�p�X>  �o�͌��ʏ���JSON�o�͂���ꍇ�͏o�̓t�@�C���p�X���w��[]\n"
		"  --mode <���[�h>     �������[�h[ts]\n"
		"                      ts : MPGE2-TS����͂���ڍ׉�̓��[�h\n"
		"                      g  : MPEG2-TS�ȊO�̓��̓t�@�C�������������ʃt�@�C�����[�h\n"
		"                           ��ʃt�@�C�����[�h��FFMPEG�Ńf�R�[�h���邽�߉��Y���␳\n"
		"                           �Ȃǂ̏����͈�؂Ȃ��̂ŁAMPEG2-TS�ɂ͎g�p���Ȃ��悤��\n"
		"  --dump              �����r���̃f�[�^���_���v�i�f�o�b�O�p�j\n",
		bin);
}

static std::tstring getParam(int argc, const tchar* argv[], int ikey) {
	if (ikey + 1 >= argc) {
		THROWF(FormatException,
			"%" PRITSTR "�I�v�V�����̓p�����[�^���K�v�ł�", argv[ikey]);
	}
	return argv[ikey + 1];
}

static std::tstring pathNormalize(std::tstring path) {
	if (path.size() != 0) {
		// �o�b�N�X���b�V���̓X���b�V���ɕϊ�
		std::replace(path.begin(), path.end(), _T('\\'), _T('/'));
		// �Ō�̃X���b�V���͎��
		if (path.back() == _T('/')) {
			path.pop_back();
		}
	}
	return path;
}

template <typename STR>
static size_t pathGetExtensionSplitPos(const STR& path) {
	size_t lastsplit = path.rfind(_T('/'));
	size_t namebegin = (lastsplit == STR::npos)
		? 0
		: lastsplit + 1;
	size_t dotpos = path.rfind(_T('.'));
	size_t len = (dotpos == STR::npos || dotpos < namebegin)
		? path.size()
		: dotpos;
	return len;
}

static std::tstring pathGetDirectory(const std::tstring& path) {
	size_t lastsplit = path.rfind(_T('/'));
	size_t namebegin = (lastsplit == std::tstring::npos)
		? 0
		: lastsplit;
	return path.substr(0, namebegin);
}

static std::tstring pathRemoveExtension(const std::tstring& path) {
	return path.substr(0, pathGetExtensionSplitPos(path));
}

static std::string pathGetExtension(const std::string& path) {
	auto ext = path.substr(pathGetExtensionSplitPos(path));
	std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
	return ext;
}

static ENUM_ENCODER encoderFtomString(const std::tstring& str) {
	if (str == _T("x264")) {
		return ENCODER_X264;
	}
	else if (str == _T("x265")) {
		return ENCODER_X265;
	}
	else if (str == _T("qsv") || str == _T("QSVEnc")) {
		return ENCODER_QSVENC;
	}
	else if (str == _T("nvenc") || str == _T("NVEnc")) {
		return ENCODER_NVENC;
	}
	return (ENUM_ENCODER)-1;
}

static DECODER_TYPE decoderFromString(const std::tstring& str) {
	if (str == _T("default")) {
		return DECODER_DEFAULT;
	}
	else if (str == _T("qsv") || str == _T("QSV")) {
		return DECODER_QSV;
	}
	else if (str == _T("cuvid") || str == _T("CUVID") || str == _T("nvdec") || str == _T("NVDec")) {
		return DECODER_CUVID;
	}
	return (DECODER_TYPE)-1;
}

static std::unique_ptr<ConfigWrapper> parseArgs(AMTContext& ctx, int argc, const tchar* argv[])
{
	std::string moduleDir = GetModuleDirectory();
	Config conf = Config();
	conf.workDir = "./";
	conf.encoderPath = "x264.exe";
	conf.encoderOptions = "";
	conf.timelineditorPath = "timelineeditor.exe";
	conf.mp4boxPath = "mp4box.exe";
	conf.chapterExePath = "chapter_exe.exe";
	conf.joinLogoScpPath = "join_logo_scp.exe";
	conf.drcsOutPath = moduleDir + "\\..\\drcs";
	conf.drcsMapPath = conf.drcsOutPath + "\\drcs_map.txt";
	conf.joinLogoScpCmdPath = moduleDir + "\\..\\JL\\JL_�W��.txt";
	conf.mode = "ts";
	conf.modeArgs = "";
	conf.bitrateCM = 1.0;
	conf.serviceId = -1;
	conf.cmoutmask = 1;

	for (int i = 1; i < argc; ++i) {
		std::tstring key = argv[i];
		if (key == _T("-i") || key == _T("--input")) {
			conf.srcFilePath = to_string(pathNormalize(getParam(argc, argv, i++)));
		}
		else if (key == _T("-o") || key == _T("--output")) {
			conf.outVideoPath =
				to_string(pathRemoveExtension(pathNormalize(getParam(argc, argv, i++))));
		}
		else if (key == _T("--mode")) {
			conf.mode = to_string(getParam(argc, argv, i++));
		}
		else if (key == _T("-a") || key == _T("--args")) {
			conf.modeArgs = to_string(getParam(argc, argv, i++));
		}
		else if (key == _T("-w") || key == _T("--work")) {
			conf.workDir = to_string(pathNormalize(getParam(argc, argv, i++)));
			if (conf.workDir.size() == 0) {
				THROW(RuntimeException, "... uha");
				conf.workDir = "./";
			}
		}
		else if (key == _T("-et") || key == _T("--encoder-type")) {
			std::tstring arg = getParam(argc, argv, i++);
			conf.encoder = encoderFtomString(arg);
			if (conf.encoder == (ENUM_ENCODER)-1) {
				PRINTF("--encoder-type�̎w�肪�Ԉ���Ă��܂�: %" PRITSTR "\n", arg.c_str());
			}
		}
		else if (key == _T("-e") || key == _T("--encoder")) {
			conf.encoderPath = to_string(getParam(argc, argv, i++));
		}
		else if (key == _T("-eo") || key == _T("--encoder-option")) {
			conf.encoderOptions = to_string(getParam(argc, argv, i++));
		}
		else if (key == _T("-b") || key == _T("--bitrate")) {
			const auto arg = getParam(argc, argv, i++);
			int ret = stscanf(arg.c_str(), _T("%lf:%lf:%lf:%lf"),
				&conf.bitrate.a, &conf.bitrate.b, &conf.bitrate.h264, &conf.bitrate.h265);
			if (ret < 3) {
				THROWF(ArgumentException, "--bitrate�̎w�肪�Ԉ���Ă��܂�");
			}
			if (ret <= 3) {
				conf.bitrate.h265 = 2;
			}
			conf.autoBitrate = true;
		}
		else if (key == _T("-bcm") || key == _T("--bitrate-cm")) {
			const auto arg = getParam(argc, argv, i++);
			int ret = stscanf(arg.c_str(), _T("%lf"), &conf.bitrateCM);
			if (ret == 0) {
				THROWF(ArgumentException, "--bitrate-cm�̎w�肪�Ԉ���Ă��܂�");
			}
		}
		else if (key == _T("--2pass")) {
			conf.twoPass = true;
		}
		else if (key == _T("--splitsub")) {
			conf.splitSub = true;
		}
		else if (key == _T("-fmt") || key == _T("--format")) {
			const auto arg = getParam(argc, argv, i++);
			if (arg == _T("mp4")) {
				conf.format = FORMAT_MP4;
			}
			else if (arg == _T("mkv")) {
				conf.format = FORMAT_MKV;
			}
			else {
				THROWF(ArgumentException, "--format�̎w�肪�Ԉ���Ă��܂�: %" PRITSTR "", arg.c_str());
			}
		}
		else if (key == _T("--chapter")) {
			conf.chapter = true;
		}
		else if (key == _T("--subtitles")) {
			conf.subtitles = true;
		}
		else if (key == _T("-m") || key == _T("--muxer")) {
			conf.muxerPath = to_string(getParam(argc, argv, i++));
		}
		else if (key == _T("-t") || key == _T("--timelineeditor")) {
			conf.timelineditorPath = to_string(getParam(argc, argv, i++));
		}
		else if (key == _T("--mp4box")) {
			conf.mp4boxPath = to_string(getParam(argc, argv, i++));
		}
		else if (key == _T("-j") || key == _T("--json")) {
			conf.outInfoJsonPath = to_string(getParam(argc, argv, i++));
		}
		else if (key == _T("-f") || key == _T("--filter")) {
			conf.filterScriptPath = to_string(getParam(argc, argv, i++));
		}
		else if (key == _T("-pf") || key == _T("--postfilter")) {
			conf.postFilterScriptPath = to_string(getParam(argc, argv, i++));
		}
		else if (key == _T("-s") || key == _T("--serivceid")) {
			std::tstring sidstr = getParam(argc, argv, i++);
			if (sidstr.size() > 2 && sidstr.substr(0, 2) == _T("0x")) {
				// 16�i
				conf.serviceId = std::stoi(sidstr.substr(2), NULL, 16);;
			}
			else {
				// 10�i
				conf.serviceId = std::stoi(sidstr);
			}
		}
		else if (key == _T("--mpeg2decoder")) {
			std::tstring arg = getParam(argc, argv, i++);
			conf.decoderSetting.mpeg2 = decoderFromString(arg);
			if (conf.decoderSetting.mpeg2 == (DECODER_TYPE)-1) {
				PRINTF("--mpeg2decoder�̎w�肪�Ԉ���Ă��܂�: %" PRITSTR "\n", arg.c_str());
			}
		}
		else if (key == _T("--h264decoder")) {
			std::tstring arg = getParam(argc, argv, i++);
			conf.decoderSetting.h264 = decoderFromString(arg);
			if (conf.decoderSetting.h264 == (DECODER_TYPE)-1) {
				PRINTF("--h264decoder�̎w�肪�Ԉ���Ă��܂�: %" PRITSTR "\n", arg.c_str());
			}
		}
		else if (key == _T("--ignore-no-logo")) {
			conf.ignoreNoLogo = true;
		}
		else if (key == _T("--ignore-no-drcsmap")) {
			conf.ignoreNoDrcsMap = true;
		}
		else if (key == _T("--logo")) {
			conf.logoPath.push_back(to_string(getParam(argc, argv, i++)));
		}
		else if (key == _T("--drcs")) {
			auto path = getParam(argc, argv, i++);
			conf.drcsMapPath = to_string(path);
			conf.drcsOutPath = to_string(pathGetDirectory(pathNormalize(path)));
			if(conf.drcsOutPath.size() == 0) {
				conf.drcsOutPath = ".";
			}
		}
		else if (key == _T("--chapter-exe")) {
			conf.chapterExePath = to_string(getParam(argc, argv, i++));
		}
		else if (key == _T("--jls")) {
			conf.joinLogoScpPath = to_string(getParam(argc, argv, i++));
		}
		else if (key == _T("--jls-cmd")) {
			conf.joinLogoScpCmdPath = to_string(getParam(argc, argv, i++));
		}
		else if (key == _T("-om") || key == _T("--cmoutmask")) {
			conf.cmoutmask = std::stol(getParam(argc, argv, i++));
		}
		else if (key == _T("--dump")) {
			conf.dumpStreamInfo = true;
		}
		else if (key == _T("--systemavsplugin")) {
			conf.systemAvsPlugin = true;
		}
		else if (key.size() == 0) {
			continue;
		}
		else {
			// �Ȃ���%ls�Œ���������H�킷�Ɨ�����̂�%s�ŕ\��
			THROWF(FormatException, "�s���ȃI�v�V����: %s", to_string(argv[i]).c_str());
		}
	}

	// muxer�̃f�t�H���g�l
	if (conf.muxerPath.size() == 0) {
		if (conf.format == FORMAT_MP4) {
			conf.muxerPath = "muxer.exe";
		}
		else {
			conf.muxerPath = "mkvmerge.exe";
		}
	}

	if (conf.mode == "ts" || conf.mode == "g") {
		if (conf.srcFilePath.size() == 0) {
			THROWF(ArgumentException, "���̓t�@�C�����w�肵�Ă�������");
		}
		if (conf.outVideoPath.size() == 0) {
			THROWF(ArgumentException, "�o�̓t�@�C�����w�肵�Ă�������");
		}
	}

	if (conf.chapter && !conf.ignoreNoLogo) {
		if (conf.logoPath.size() == 0) {
			THROW(ArgumentException, "���S���w�肳��Ă��܂���");
		}
	}

	// CM��͂͂S������K�v������
	if (conf.chapterExePath.size() > 0 || conf.joinLogoScpPath.size() > 0) {
		if (conf.chapterExePath.size() == 0) {
			THROW(ArgumentException, "chapter_exe.exe�ւ̃p�X���ݒ肳��Ă��܂���");
		}
		if (conf.joinLogoScpPath.size() == 0) {
			THROW(ArgumentException, "join_logo_scp.exe�ւ̃p�X���ݒ肳��Ă��܂���");
		}
	}

	if (conf.mode == "enctask") {
		// �K�v�Ȃ�
		conf.workDir = "";
	}

	// exe��T��
	conf.chapterExePath = SearchExe(conf.chapterExePath);
	conf.encoderPath = SearchExe(conf.encoderPath);
	conf.joinLogoScpPath = SearchExe(conf.joinLogoScpPath);
	conf.mp4boxPath = SearchExe(conf.mp4boxPath);
	conf.muxerPath = SearchExe(conf.muxerPath);
	conf.timelineditorPath = SearchExe(conf.timelineditorPath);

	return std::unique_ptr<ConfigWrapper>(new ConfigWrapper(ctx, conf));
}

static CRITICAL_SECTION g_log_crisec;
static void amatsukaze_av_log_callback(
	void* ptr, int level, const char* fmt, va_list vl)
{
	level &= 0xff;

	if (level > av_log_get_level()) {
		return;
	}

	char buf[1024];
	vsnprintf(buf, sizeof(buf), fmt, vl);
	int len = (int)strlen(buf);
	if (len == 0) {
		return;
	}

	static char* log_levels[] = {
		"panic", "fatal", "error", "warn", "info", "verb", "debug", "trace"
	};

	EnterCriticalSection(&g_log_crisec);
	
	static bool print_prefix = true;
	bool tmp_pp = print_prefix;
	print_prefix = (buf[len - 1] == '\r' || buf[len - 1] == '\n');
	if (tmp_pp) {
		int logtype = level / 8;
		const char* level_str =
			(logtype >= sizeof(log_levels) / sizeof(log_levels[0]))
			? "unk" : log_levels[logtype];
		fprintf(stderr, "FFMPEG [%s] %s", level_str, buf);
	}
	else {
		fprintf(stderr, buf);
	}
	if (print_prefix) {
		fflush(stdout);
	}

	LeaveCriticalSection(&g_log_crisec);
}

static int amatsukazeTranscodeMain(AMTContext& ctx, const ConfigWrapper& setting) {
	try {

		if (setting.isSubtitlesEnabled()) {
			// DRCS�}�b�s���O�����[�h
			ctx.loadDRCSMapping(setting.getDRCSMapPath());
		}

		std::string mode = setting.getMode();
		if (mode == "ts")
			transcodeMain(ctx, setting);
		else if (mode == "g")
			transcodeSimpleMain(ctx, setting);

		else if (mode == "test_print_crc")
			test::PrintCRCTable(ctx, setting);
		else if (mode == "test_crc")
			test::CheckCRC(ctx, setting);
		else if (mode == "test_read_bits")
			test::ReadBits(ctx, setting);
		else if (mode == "test_auto_buffer")
			test::CheckAutoBuffer(ctx, setting);
		else if (mode == "test_verifympeg2ps")
			test::VerifyMpeg2Ps(ctx, setting);
		else if (mode == "test_readts")
			test::ReadTS(ctx, setting);
		else if (mode == "test_aacdec")
			test::AacDecode(ctx, setting);
		else if (mode == "test_wavewrite")
			test::WaveWriteHeader(ctx, setting);
		else if (mode == "test_process")
			test::ProcessTest(ctx, setting);
		else if (mode == "test_streamreform")
			test::FileStreamInfo(ctx, setting);
		else if (mode == "test_parseargs")
			test::ParseArgs(ctx, setting);
		else if (mode == "test_lossless")
			test::LosslessFileTest(ctx, setting);
		else if (mode == "test_logoframe")
			test::LogoFrameTest(ctx, setting);
		else if (mode == "test_dualmono")
			test::SplitDualMonoAAC(ctx, setting);
		else if (mode == "test_aacdecode")
			test::AACDecodeTest(ctx, setting);
		else if (mode == "test_ass")
			test::CaptionASS(ctx, setting);
		else if (mode == "test_eo")
			test::EncoderOptionParse(ctx, setting);

		else
			PRINTF("--mode�̎w�肪�Ԉ���Ă��܂�: %s\n", mode.c_str());

		return 0;
	}
	catch (const NoLogoException&) {
		// ���S������100�Ƃ���
		return 100;
	}
	catch (const NoDrcsMapException&) {
		// DRCS�}�b�s���O�Ȃ���101�Ƃ���
		return 101;
	}
	catch (const AvisynthError& avserror) {
		ctx.error("AviSynth Error");
		ctx.error(avserror.msg);
		return 2;
	}
	catch (const Exception&) {
		return 1;
	}
}

__declspec(dllexport) int AmatsukazeCLI(int argc, const wchar_t* argv[]) {
	try {
		printCopyright();

		AMTContext ctx;

		ctx.setDefaultCP();

		auto setting = parseArgs(ctx, argc, argv);

		// FFMPEG���C�u����������
		InitializeCriticalSection(&g_log_crisec);
		av_log_set_callback(amatsukaze_av_log_callback);
		av_register_all();

		// �L���v�V����DLL������
		InitializeCPW();

		return amatsukazeTranscodeMain(ctx, *setting);
	}
	catch (const Exception&) {
		// parseArgs�ŃG���[
		printHelp(argv[0]);
		return 1;
	}
}
