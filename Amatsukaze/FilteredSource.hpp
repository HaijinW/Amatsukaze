/**
* Create encoder source with avisynth filters
* Copyright (c) 2017-2019 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include <memory>
#include <numeric>
#include <regex>

#include "StreamUtils.hpp"
#include "ReaderWriterFFmpeg.hpp"
#include "TranscodeSetting.hpp"
#include "StreamReform.hpp"
#include "AMTSource.hpp"
#include "InterProcessComm.hpp"

// Defined in ComputeKernel.cpp
bool IsAVXAvailable();
bool IsAVX2Available();

class RFFExtractor
{
public:
	void clear() {
		prevFrame_ = nullptr;
	}

	void inputFrame(av::EncodeWriter& encoder, std::unique_ptr<av::Frame>&& frame, PICTURE_TYPE pic) {

		// PTS��inputFrame�ōĒ�`�����̂ŏC�����Ȃ��ł��̂܂ܓn��
		switch (pic) {
		case PIC_FRAME:
		case PIC_TFF:
		case PIC_TFF_RFF:
			encoder.inputFrame(*frame);
			break;
		case PIC_FRAME_DOUBLING:
			encoder.inputFrame(*frame);
			encoder.inputFrame(*frame);
			break;
		case PIC_FRAME_TRIPLING:
			encoder.inputFrame(*frame);
			encoder.inputFrame(*frame);
			encoder.inputFrame(*frame);
			break;
		case PIC_BFF:
			encoder.inputFrame(*mixFields(
				(prevFrame_ != nullptr) ? *prevFrame_ : *frame, *frame));
			break;
		case PIC_BFF_RFF:
			encoder.inputFrame(*mixFields(
				(prevFrame_ != nullptr) ? *prevFrame_ : *frame, *frame));
			encoder.inputFrame(*frame);
			break;
		}

		prevFrame_ = std::move(frame);
	}

private:
	std::unique_ptr<av::Frame> prevFrame_;

	// 2�̃t���[���̃g�b�v�t�B�[���h�A�{�g���t�B�[���h������
	static std::unique_ptr<av::Frame> mixFields(av::Frame& topframe, av::Frame& bottomframe)
	{
		auto dstframe = std::unique_ptr<av::Frame>(new av::Frame());

		AVFrame* top = topframe();
		AVFrame* bottom = bottomframe();
		AVFrame* dst = (*dstframe)();

		// �t���[���̃v���p�e�B���R�s�[
		av_frame_copy_props(dst, top);

		// �������T�C�Y�Ɋւ�������R�s�[
		dst->format = top->format;
		dst->width = top->width;
		dst->height = top->height;

		// �������m��
		if (av_frame_get_buffer(dst, 64) != 0) {
			THROW(RuntimeException, "failed to allocate frame buffer");
		}

		const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get((AVPixelFormat)(dst->format));
		int pixel_shift = (desc->comp[0].depth > 8) ? 1 : 0;
		int nplanes = (dst->format != AV_PIX_FMT_NV12) ? 3 : 2;

		for (int i = 0; i < nplanes; ++i) {
			int hshift = (i > 0 && dst->format != AV_PIX_FMT_NV12) ? desc->log2_chroma_w : 0;
			int vshift = (i > 0) ? desc->log2_chroma_h : 0;
			int wbytes = (dst->width >> hshift) << pixel_shift;
			int height = dst->height >> vshift;

			for (int y = 0; y < height; y += 2) {
				uint8_t* dst0 = dst->data[i] + dst->linesize[i] * (y + 0);
				uint8_t* dst1 = dst->data[i] + dst->linesize[i] * (y + 1);
				uint8_t* src0 = top->data[i] + top->linesize[i] * (y + 0);
				uint8_t* src1 = bottom->data[i] + bottom->linesize[i] * (y + 1);
				memcpy(dst0, src0, wbytes);
				memcpy(dst1, src1, wbytes);
			}
		}

		return std::move(dstframe);
	}
};

static PICTURE_TYPE getPictureTypeFromAVFrame(AVFrame* frame)
{
	bool interlaced = frame->interlaced_frame != 0;
	bool tff = frame->top_field_first != 0;
	int repeat = frame->repeat_pict;
	if (interlaced == false) {
		switch (repeat) {
		case 0: return PIC_FRAME;
		case 1: return tff ? PIC_TFF_RFF : PIC_BFF_RFF;
		case 2: return PIC_FRAME_DOUBLING;
		case 4: return PIC_FRAME_TRIPLING;
		default: THROWF(FormatException, "Unknown repeat count: %d", repeat);
		}
		return PIC_FRAME;
	}
	else {
		if (repeat) {
			THROW(FormatException, "interlaced and repeat ???");
		}
		return tff ? PIC_TFF : PIC_BFF;
	}
}

class AMTFilterSource : public AMTObject {
	class AvsScript
	{
	public:
		StringBuilder& Get() { return append; }
		void Apply(IScriptEnvironment* env) {
			auto str = append.str();
			if (str.size() > 0) {
				append.clear();
				script += str;
				// �Ō�̌��ʂ͎�����last�ɓ���Ȃ���΂Ȃ�Ȃ����Ƃɒ���
				//�i��������Ȃ��ƍŌ�̃t�B���^�Ăяo���̒��O��last�ɂȂ��Ă��܂��j
				env->SetVar("last", env->Invoke("Eval", str.c_str()));
			}
		}
		void Clear() {
			script.clear();
			append.clear();
		}
		const std::string& Str() const {
			return script;
		}
	private:
		std::string script;
		StringBuilder append;
	};

	void readTimecodeFile(const tstring& filepath) {
		File file(filepath, _T("r"));
		std::regex re("#\\s*total:\\s*([+-]?([0-9]*[.])?[0-9]+).*");
		std::string str;
		timeCodes_.clear();
		while (file.getline(str)) {
			if (str.size()) {
				std::smatch m;
				if (std::regex_search(str, m, re)) {
					timeCodes_.push_back(std::atof(m[1].str().c_str()) * 1000);
					return;
				}
				else if (str[0] != '#') {
					timeCodes_.push_back(std::atoi(str.c_str()));
				}
			}
		}
		// ���v���Ԃ𐄑�
		size_t numFrames = timeCodes_.size();
		if (numFrames >= 2) {
			timeCodes_.push_back(timeCodes_[numFrames - 1] * 2 - timeCodes_[numFrames - 2]);
		}
		else if (numFrames == 1) {
			timeCodes_.push_back(timeCodes_[0] + 1000.0 / 60.0);
		}
	}

	void readTimecode(EncodeFileKey key) {
		auto timecodepath = setting_.getAvsTimecodePath(key);
		// timecode�t�@�C������������ǂݍ���
		if (File::exists(timecodepath)) {
			readTimecodeFile(timecodepath);
			// �x�[�XFPS�𐄑�
			// �t���[���^�C�~���O�Ƃ̍��̘a���ł�������FPS���x�[�XFPS�Ƃ���
			double minDiff = timeCodes_.back();
			double epsilon = timeCodes_.size() * 10e-10;
			for (auto fps : { 60, 120, 240 }) {
				double mult = fps / 1001.0;
				double inv = 1.0 / mult;
				double diff = 0;
				for (auto ts : timeCodes_) {
					diff += std::abs(inv * std::round(ts * mult) - ts);
				}
				if (diff < minDiff - epsilon) {
					vfrTimingFps_ = fps;
					minDiff = diff;
				}
			}
		}
	}

public:
	// Main (+ Post)
	AMTFilterSource(AMTContext&ctx,
		const ConfigWrapper& setting,
		const StreamReformInfo& reformInfo,
		const std::vector<EncoderZone>& zones,
		const tstring& logopath,
		EncodeFileKey key,
		const ResourceManger& rm)
		: AMTObject(ctx)
		, setting_(setting)
		, env_(make_unique_ptr((IScriptEnvironment2*)nullptr))
		, vfrTimingFps_(0)
	{
		try {
			// �t�B���^�O�����p���\�[�X�m��
			auto res = rm.wait(HOST_CMD_Filter);

			int pass = 0;
			for (; pass < 4; ++pass) {
				if(!FilterPass(pass, res.gpuIndex, key, reformInfo, logopath)) {
					break;
				}
				ReadAllFrames(pass);
			}

			// �G���R�[�h�p���\�[�X�m��
			auto encodeRes = rm.request(HOST_CMD_Encode);
			if (encodeRes.IsFailed() || encodeRes.gpuIndex != res.gpuIndex) {
				// �m�ۂł��Ȃ����� or GPU���ύX���ꂽ�� ��U�������
				env_ = nullptr;
				if (encodeRes.IsFailed()) {
					// ���\�[�X���m�ۂł��Ă��Ȃ�������m�ۂł���܂ő҂�
					encodeRes = rm.wait(HOST_CMD_Encode);
				}
			}

			// �G���R�[�h�p���\�[�X�ŃA�t�B�j�e�B��ݒ�
			res = encodeRes;
			SetCPUAffinity(res.group, res.mask);
			if (env_ == nullptr) {
				FilterPass(pass, res.gpuIndex, key, reformInfo, logopath);
			}

			auto& sb = script_.Get();
			tstring postpath = setting.getPostFilterScriptPath();
			if (postpath.size()) {
				sb.append("AMT_SOURCE = last\n");
				sb.append("Import(\"%s\")\n", postpath);
			}

			auto durationpath = setting_.getAvsDurationPath(key);
			// duration�t�@�C����AMTDecimate������
			if (File::exists(durationpath)) {
				sb.append("AMTDecimate(\"%s\")\n", durationpath);
			}

			readTimecode(key);

			if (setting_.isDumpFilter()) {
				sb.append("DumpFilterGraph(\"%s\", 1)\n",
					setting_.getFilterGraphDumpPath(key));
				// �������f�o�b�O�p 2000�t���[�����ƂɃO���t�_���v
				//sb.append("DumpFilterGraph(\"%s\", 2, 2000, true)\n",
				//	setting_.getFilterGraphDumpPath(fileId, encoderId, cmtype));
			}

			script_.Apply(env_.get());
			filter_ = env_->GetVar("last").AsClip();
			writeScriptFile(key);

			MakeZones(key, zones, reformInfo);

			MakeOutFormat(reformInfo.getFormat(key).videoFormat);
		}
		catch (const AvisynthError& avserror) {
			// �f�o�b�O�p�ɃX�N���v�g�͕ۑ����Ă���
			writeScriptFile(key);
			// AvisynthError��ScriptEnvironment�Ɉˑ����Ă���̂�
			// AviSynthException�ɕϊ�����
			THROWF(AviSynthException, "%s", avserror.msg);
		}
	}

	~AMTFilterSource() {
		filter_ = nullptr;
		env_ = nullptr;
	}

	const PClip& getClip() const {
		return filter_;
	}

	std::string getScript() const {
		return script_.Str();
	}

	const VideoFormat& getFormat() const {
		return outfmt_;
	}

	// ���̓]�[����trim��̃]�[����Ԃ�
	const std::vector<EncoderZone> getZones() const {
		return outZones_;
	}

	// �e�t���[���̎���ms(�Ō�̃t���[���̕\�����Ԃ��`���邽�ߗv�f���̓t���[����+1)
	const std::vector<double>& getTimeCodes() const {
		return timeCodes_;
	}

	int getVfrTimingFps() const {
		return vfrTimingFps_;
	}

	IScriptEnvironment2* getEnv() const {
		return env_.get();
	}

private:
	const ConfigWrapper& setting_;
	ScriptEnvironmentPointer env_;
	AvsScript script_;
	PClip filter_;
	VideoFormat outfmt_;
	std::vector<EncoderZone> outZones_;
	std::vector<double> timeCodes_;
	int vfrTimingFps_;

	void writeScriptFile(EncodeFileKey key) {
		auto& str = script_.Str();
		File avsfile(setting_.getFilterAvsPath(key), _T("w"));
		avsfile.write(MemoryChunk((uint8_t*)str.c_str(), str.size()));
	}

	std::vector<tstring> GetSuitablePlugins(const tstring& basepath) {
		struct Plugin {
			tstring FileName;
			tstring BaseName;
		};
		if (DirectoryExists(basepath) == false) return std::vector<tstring>();
		std::vector<tstring> categories = { _T("_avx2.dll"), _T("_avx.dll"), _T(".dll") };
		std::vector<std::vector<Plugin>> categoryList(categories.size());
		for (tstring filename : GetDirectoryFiles(basepath, _T("*.dll"))) {
			std::transform(filename.begin(), filename.end(), filename.begin(), ::tolower);
			for (int i = 0; i < (int)categories.size(); ++i) {
				const auto& category = categories[i];
				if (ends_with(filename, category)) {
					auto baseName = filename.substr(0, filename.size() - category.size());
					Plugin plugin = { filename, baseName };
					categoryList[i].push_back(plugin);
					break;
				}
			}
		}
		int support = 2;
		if (IsAVX2Available()) {
			support = 0;
		}
		else if (IsAVXAvailable()) {
			support = 1;
		}
		// BaseName -> FileName
		std::map<tstring, tstring> pluginMap;
		for (int i = (int)categories.size() - 1; i >= support; --i) {
			for (auto& plugin : categoryList[i]) {
				pluginMap[plugin.BaseName] = plugin.FileName;
			}
		}
		std::vector<tstring> result(pluginMap.size());
		std::transform(pluginMap.begin(), pluginMap.end(), result.begin(),
			[&](const std::pair<tstring, tstring>& entry) { return basepath + _T("\\") + entry.second; });
		return result;
	}

	void InitEnv() {
		env_ = nullptr;
		env_ = make_unique_ptr(CreateScriptEnvironment2());

		script_.Clear();
		auto& sb = script_.Get();
		if (setting_.isDumpFilter()) {
			sb.append("SetGraphAnalysis(true)\n");
		}
		// �V�X�e���̃v���O�C���t�H���_�𖳌���
		if (setting_.isSystemAvsPlugin() == false) {
			sb.append("ClearAutoloadDirs()\n");
		}
		auto moduleDir = GetModuleDirectory();
		// Amatsukaze�p�I�[�g���[�h�t�H���_��ǉ�
		sb.append("AddAutoloadDir(\"%s\\plugins64\")\n", moduleDir);
		// AutoSelect�v���O�C�������[�h
		for (auto& path : GetSuitablePlugins(moduleDir + _T("\\plugins64\\AutoSelected"))) {
			sb.append("LoadPlugin(\"%s\")\n", path);
		}
		// �������ߖ�I�v�V������L���ɂ���
		sb.append("SetCacheMode(CACHE_OPTIMAL_SIZE)\n");
		// ����̓������s���ɂȂ�Ƌɒ[�ɐ��\��������̂ł�߂�
		//sb.append("SetDeviceOpt(DEV_FREE_THRESHOLD, 1000)\n");
		// Amatsukaze.dll�����[�h
		sb.append("LoadPlugin(\"%s\")\n", GetModulePath());
	}

	void ReadAllFrames(int pass) {
		PClip clip = env_->GetVar("last").AsClip();
		const VideoInfo vi = clip->GetVideoInfo();

		ctx.infoF("�t�B���^�p�X%d �\��t���[����: %d", pass + 1, vi.num_frames);
		Stopwatch sw;
		sw.start();
		int prevFrames = 0;

		for (int i = 0; i < vi.num_frames; ++i) {
			PVideoFrame frame = clip->GetFrame(i, env_.get());
			double elapsed = sw.current();
			if (elapsed >= 1.0) {
				double fps = (i - prevFrames) / elapsed;
				ctx.progressF("%d�t���[������ %.2ffps", i + 1, fps);

				prevFrames = i;
				sw.stop();
			}
		}

		ctx.infoF("�t�B���^�p�X%d ����: %.2f�b", pass + 1, sw.getTotal());
	}

	void defineMakeSource(
		EncodeFileKey key,
		const StreamReformInfo& reformInfo,
		const tstring& logopath)
	{
		auto& sb = script_.Get();
		sb.append("function MakeSource(bool \"mt\") {\n");
		sb.append("\tmt = default(mt, false)\n");
		sb.append("\tAMTSource(\"%s\")\n", setting_.getTmpAMTSourcePath(key.video));
		sb.append("\tif(mt) { Prefetch(1, 4) }\n");

		if (setting_.isNoDelogo() == false && logopath.size() > 0) {
			sb.append("\tlogo = \"%s\"\n", logopath);
			sb.append("\tAMTEraseLogo(AMTAnalyzeLogo(logo), logo, \"%s\", maxfade=%d)\n",
				setting_.getTmpLogoFramePath(key.video), setting_.getMaxFadeLength());
			sb.append("\tif(mt) { Prefetch(1, 4) }\n");
		}

		sb.append("\t");
		trimInput(key, reformInfo);
		sb.append("}\n");
	}

	void trimInput(EncodeFileKey key,
		const StreamReformInfo& reformInfo)
	{
		// ����encoderIndex+cmtype�p�̏o�̓t���[�����X�g�쐬
		const auto& srcFrames = reformInfo.getFilterSourceAudioFrames(key.video);
		const auto& outFrames = reformInfo.getEncodeFile(key).videoFrames;
		int numSrcFrames = (int)outFrames.size();

		// �s�A���_�ŋ�؂�
		std::vector<EncoderZone> trimZones;
		EncoderZone zone;
		zone.startFrame = outFrames.front();
		for (int i = 1; i < (int)outFrames.size(); ++i) {
			if (outFrames[i] != outFrames[i - 1] + 1) {
				zone.endFrame = outFrames[i - 1];
				trimZones.push_back(zone);
				zone.startFrame = outFrames[i];
			}
		}
		zone.endFrame = outFrames.back();
		trimZones.push_back(zone);

		auto& sb = script_.Get();
		if (trimZones.size() > 1 ||
			trimZones[0].startFrame != 0 ||
			trimZones[0].endFrame != (srcFrames.size() - 1))
		{
			// Trim���K�v
			for (int i = 0; i < (int)trimZones.size(); ++i) {
				if (i > 0) sb.append("++");
				sb.append("Trim(%d,%d)", trimZones[i].startFrame, trimZones[i].endFrame);
			}
			sb.append("\n");
		}
	}

	// �߂�l: �O�����H
	bool FilterPass(int pass, int gpuIndex,
		EncodeFileKey key,
		const StreamReformInfo& reformInfo,
		const tstring& logopath)
	{
		InitEnv();

		auto tmppath = setting_.getAvsTmpPath(key);

		defineMakeSource(key, reformInfo, logopath);

		auto& sb = script_.Get();
		sb.append("AMT_SOURCE = MakeSource(true)\n");
		sb.append("AMT_TMP = \"%s\"\n", pathToOS(tmppath));
		sb.append("AMT_PASS = %d\n", pass);
		sb.append("AMT_DEV = %d\n", gpuIndex);
		sb.append("AMT_SOURCE\n");

		tstring mainpath = setting_.getFilterScriptPath();
		if (mainpath.size()) {
			sb.append("Import(\"%s\")\n", mainpath);
		}

		script_.Apply(env_.get());
		return env_->GetVarDef("AMT_PRE_PROC", false).AsBool();
	}

	void MakeZones(
		EncodeFileKey key,
		const std::vector<EncoderZone>& zones,
		const StreamReformInfo& reformInfo)
	{
		const auto& outFrames = reformInfo.getEncodeFile(key).videoFrames;

		// ����encoderIndex�p�̃]�[�����쐬
		outZones_.clear();
		for (int i = 0; i < (int)zones.size(); ++i) {
			EncoderZone newZone = {
			  (int)(std::lower_bound(outFrames.begin(), outFrames.end(), zones[i].startFrame) - outFrames.begin()),
			  (int)(std::lower_bound(outFrames.begin(), outFrames.end(), zones[i].endFrame) - outFrames.begin())
			};
			// �Z������ꍇ�̓]�[�����̂Ă�
			if (newZone.endFrame - newZone.startFrame > 30) {
				outZones_.push_back(newZone);
			}
		}

		int numSrcFrames = (int)outFrames.size();

		VideoInfo outvi = filter_->GetVideoInfo();
		int numOutFrames = outvi.num_frames;

		const VideoFormat& infmt = reformInfo.getFormat(key).videoFormat;
		double srcDuration = (double)numSrcFrames * infmt.frameRateDenom / infmt.frameRateNum;
		double clipDuration = timeCodes_.size()
			? timeCodes_.back() / 1000.0
			: (double)numOutFrames * outvi.fps_denominator / outvi.fps_numerator;
		bool outParity = filter_->GetParity(0);

		ctx.infoF("�t�B���^����: %d�t���[�� %d/%dfps (%s)",
			numSrcFrames, infmt.frameRateNum, infmt.frameRateDenom,
			infmt.progressive ? "�v���O���b�V�u" : "�C���^�[���[�X");

		if (timeCodes_.size()) {
			ctx.infoF("�t�B���^�o��: %d�t���[�� VFR (�x�[�X %d/%d fps)",
				numOutFrames, outvi.fps_numerator, outvi.fps_denominator);
		}
		else {
			ctx.infoF("�t�B���^�o��: %d�t���[�� %d/%dfps (%s)",
				numOutFrames, outvi.fps_numerator, outvi.fps_denominator,
				outParity ? "�C���^�[���[�X" : "�v���O���b�V�u");
		}

		if (std::abs(srcDuration - clipDuration) > 0.1f) {
			THROWF(RuntimeException, "�t�B���^�o�͉f���̎��Ԃ����͂ƈ�v���܂���i����: %.3f�b �o��: %.3f�b�j", srcDuration, clipDuration);
		}

		if (numSrcFrames != numOutFrames && outParity) {
			ctx.warn("�t���[�������ς���Ă��܂����C���^�[���[�X�̂܂܂ł��B�v���O���b�V�u�o�͂��ړI�Ȃ�AssumeBFF()��avs�t�@�C���̍Ō�ɒǉ����Ă��������B");
		}

		if (timeCodes_.size()) {
			// VFR�^�C���X�^���v��outZones�ɔ��f������
			double tick = (double)infmt.frameRateDenom / infmt.frameRateNum;
			for (int i = 0; i < (int)outZones_.size(); ++i) {
				outZones_[i].startFrame = std::lower_bound(timeCodes_.begin(), timeCodes_.end(), outZones_[i].startFrame * tick * 1000) - timeCodes_.begin();
				outZones_[i].endFrame = std::lower_bound(timeCodes_.begin(), timeCodes_.end(), outZones_[i].endFrame * tick * 1000) - timeCodes_.begin();
			}
		}
		else if (numSrcFrames != numOutFrames) {
			// �t���[�������ς���Ă���ꍇ�̓]�[���������L�΂�
			double scale = (double)numOutFrames / numSrcFrames;
			for (int i = 0; i < (int)outZones_.size(); ++i) {
				outZones_[i].startFrame = std::max(0, std::min(numOutFrames, (int)std::round(outZones_[i].startFrame * scale)));
				outZones_[i].endFrame = std::max(0, std::min(numOutFrames, (int)std::round(outZones_[i].endFrame * scale)));
			}
		}
	}

	void MakeOutFormat(const VideoFormat& infmt)
	{
		auto vi = filter_->GetVideoInfo();
		// vi_����G���R�[�_���͗pVideoFormat�𐶐�����
		outfmt_ = infmt;
		if (outfmt_.width != vi.width || outfmt_.height != vi.height) {
			// ���T�C�Y���ꂽ
			outfmt_.width = vi.width;
			outfmt_.height = vi.height;
			// ���T�C�Y���ꂽ�ꍇ�̓A�X�y�N�g���1:1�ɂ���
			outfmt_.sarHeight = outfmt_.sarWidth = 1;
		}
		outfmt_.frameRateDenom = vi.fps_denominator;
		outfmt_.frameRateNum = vi.fps_numerator;
		// �C���^�[���[�X���ǂ����͎擾�ł��Ȃ��̂Ńp���e�B��false(BFF?)��������v���O���b�V�u�Ɖ���
		outfmt_.progressive = (filter_->GetParity(0) == false);
	}
};

class AMTDecimate : public GenericVideoFilter
{
	std::vector<int> durations;
	std::vector<int> framesMap;
public:
	AMTDecimate(PClip source, const std::string& duration, IScriptEnvironment* env)
		: GenericVideoFilter(source)
	{
		File file(to_tstring(duration), _T("r"));
		std::string str;
		while (file.getline(str)) {
			durations.push_back(std::atoi(str.c_str()));
		}
		int numSourceFrames = std::accumulate(durations.begin(), durations.end(), 0);
		if (vi.num_frames != numSourceFrames) {
			env->ThrowError("[AMTDecimate] # of frames does not match. %d(%s) vs %d(source clip)",
				(int)numSourceFrames, duration.c_str(), vi.num_frames);
		}
 		vi.num_frames = (int)durations.size();
		framesMap.resize(durations.size());
		framesMap[0] = 0;
		for (int i = 0; i < (int)durations.size() - 1; ++i) {
			framesMap[i + 1] = framesMap[i] + durations[i];
		}
	}

	PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env)
	{
		return child->GetFrame(framesMap[std::max(0, std::min(n, vi.num_frames - 1))], env);
	}

	static AVSValue __cdecl Create(AVSValue args, void* user_data, IScriptEnvironment* env)
	{
		return new AMTDecimate(
			args[0].AsClip(),       // source
			args[1].AsString(),       // analyzeclip
			env
		);
	}
};

// VFR�ł��������̃��[�g�R���g���[������������
// VFR�^�C�~���O��CM�]�[������]�[���ƃr�b�g���[�g���쐬
std::vector<BitrateZone> MakeVFRBitrateZones(const std::vector<double>& timeCodes,
	const std::vector<EncoderZone>& cmzones, double bitrateCM,
	int fpsNum, int fpsDenom, double timeFactor, double costLimit)
{
	enum {
		UNIT_FRAMES = 8,
		HARD_ZONE_LIMIT = 1000, // �]�[���������1000
		TARGET_ZONES_PER_HOUR = 30 // �ڕW�]�[������1���Ԃ�����30��
	};
	struct Block {
		int index;   // �u���b�N�擪��UNIT�A�h���X
		int next;    // ���̃u���b�N�̐擪�u���b�N�A�h���X�i���̃u���b�N�����݂��Ȃ��ꍇ��-1�j
		double avg;  // ���̃u���b�N�̕��σr�b�g���[�g
		double cost; // ���̃u���b�N�ƌ��������Ƃ��̒ǉ��R�X�g
	};

	if (timeCodes.size() == 0) {
		return std::vector<BitrateZone>();
	}
	int numFrames = (int)timeCodes.size() - 1;
	// 8�t���[�����Ƃ̕��σr�b�g���[�g���v�Z
	std::vector<double> units(nblocks(numFrames, UNIT_FRAMES));
	for (int i = 0; i < (int)units.size(); ++i) {
		auto start = timeCodes.begin() + i * UNIT_FRAMES;
		auto end = ((i + 1) * UNIT_FRAMES < timeCodes.size()) ? start + UNIT_FRAMES : timeCodes.end() - 1;
		double sum = (*end - *start) / 1000.0 * fpsNum / fpsDenom;
		double invfps = sum / (int)(end - start);
		units[i] = (invfps - 1.0) * timeFactor + 1.0;
	}
	// cmzones��K�p
	for (int i = 0; i < (int)cmzones.size(); ++i) {
		// ���[������CM�]�[����������������Ɋۂ߂�
		int start = nblocks(cmzones[i].startFrame, UNIT_FRAMES);
		int end = cmzones[i].endFrame / UNIT_FRAMES;
		for (int k = start; k < end; ++k) {
			units[k] *= bitrateCM;
		}
	}
	// �����ł�units�͊e�t���[���ɓK�p���ׂ��r�b�g���[�g
	// �����A���̂܂�zones�ɂ���Ɛ�����������
	// �R�}���h���C�������ɂł��Ȃ��̂ł�����x�܂Ƃ߂�
	std::vector<Block> blocks;
	double cur = units[0];
	blocks.push_back(Block{ 0, 1, cur, 0 });
	// �����r�b�g���[�g�̘A���͂܂Ƃ߂�
	for (int i = 1; i < (int)units.size(); ++i) {
		if (units[i] != cur) {
			cur = units[i];
			blocks.push_back(Block{ i, (int)blocks.size() + 1, cur, 0 });
		}
	}
	// �Ō�ɔԕ���u��
	blocks.push_back(Block{ (int)units.size(), -1, 0, 0 });

	auto sumDiff = [&](int start, int end, double avg) {
		double diff = 0;
		for (int i = start; i < end; ++i) {
			diff += std::abs(units[i] - avg);
		}
		return diff;
	};

	auto calcCost = [&](Block& cur, const Block&  next) {
		int start = cur.index;
		int mid = next.index;
		int end = blocks[next.next].index;
		// ���݂̃R�X�g

		double cur_cost = sumDiff(start, mid, cur.avg);
		double next_cost = sumDiff(mid, end, next.avg);
		// �A����̕��σr�b�g���[�g
		double avg2 = (cur.avg * (mid - start) + next.avg * (end - mid)) / (end - start);
		// �A����̃R�X�g
		double cost2 = sumDiff(start, end, avg2);
		// �ǉ��R�X�g
		cur.cost = cost2 - (cur_cost + next_cost);
	};

	// �A�����ǉ��R�X�g�v�Z
	for (int i = 0; blocks[i].index < (int)units.size(); i = blocks[i].next) {
		auto& cur = blocks[i];
		auto& next = blocks[cur.next];
		// ���̃u���b�N�����݂����
		if (next.index < (int)units.size()) {
			calcCost(cur, next);
		}
	}

	// �ő�u���b�N��
	auto totalHours = timeCodes.back() / 1000.0 / 3600.0;
	int targetNumZones = std::max(1, (int)(TARGET_ZONES_PER_HOUR * totalHours));
	double totalCostLimit = units.size() * costLimit;

	// �q�[�v�쐬
	auto comp = [&](int b0, int b1) {
		return blocks[b0].cost > blocks[b1].cost;
	};
	// �Ō�̃u���b�N�Ɣԕ��͘A���ł��Ȃ��̂ŏ���
	int heapSize = (int)blocks.size() - 2;
	int numZones = heapSize;
	std::vector<int> indices(heapSize);
	for (int i = 0; i < heapSize; ++i) indices[i] = i;
	std::make_heap(indices.begin(), indices.begin() + heapSize, comp);
	double totalCost = 0;
	while ((totalCost < totalCostLimit && numZones > targetNumZones) ||
		numZones > HARD_ZONE_LIMIT)
	{
		// �ǉ��R�X�g�ŏ��u���b�N
		int idx = indices.front();
		std::pop_heap(indices.begin(), indices.begin() + (heapSize--), comp);
		auto& cur = blocks[idx];
		// ���̃u���b�N�����ɘA���ς݂łȂ����
		if (cur.next != -1) {
			auto& next = blocks[cur.next];
			int start = cur.index;
			int mid = next.index;
			int end = blocks[next.next].index;
			totalCost += cur.cost;
			// �A����̕��σr�b�g���[�g�ɍX�V
			cur.avg = (cur.avg * (mid - start) + next.avg * (end - mid)) / (end - start);
			// �A�����next�ɍX�V
			cur.next = next.next;
			// �A�������u���b�N�͖�����
			next.next = -1;
			--numZones;
			// �X�Ɏ��̃u���b�N�������
			auto& nextnext = blocks[cur.next];
			if (nextnext.index < (int)units.size()) {
				// �A�����̒ǉ��R�X�g���v�Z
				calcCost(cur, nextnext);
				// �ēx�q�[�v�ɒǉ�
				indices[heapSize] = idx;
				std::push_heap(indices.begin(), indices.begin() + (++heapSize), comp);
			}
		}
	}

	// ���ʂ𐶐�
	std::vector<BitrateZone> zones;
	for (int i = 0; blocks[i].index < (int)units.size(); i = blocks[i].next) {
		const auto& cur = blocks[i];
		BitrateZone zone = BitrateZone();
		zone.startFrame = cur.index * UNIT_FRAMES;
		zone.endFrame = std::min(numFrames, blocks[cur.next].index * UNIT_FRAMES);
		zone.bitrate = cur.avg;
		zones.push_back(zone);
	}

	return zones;
}

// VFR�ɑΉ����Ă��Ȃ��G���R�[�_�Ńr�b�g���[�g�w����s���Ƃ��p��
// ���σt���[�����[�g���l�������r�b�g���[�g���v�Z����
double AdjustVFRBitrate(const std::vector<double>& timeCodes, int fpsNum, int fpsDenom)
{
	if (timeCodes.size() == 0) {
		return 1.0;
	}
	return (timeCodes.back() / 1000.0) / (timeCodes.size() - 1) * fpsNum / fpsDenom;
}

AVSValue __cdecl AMTExec(AVSValue args, void* user_data, IScriptEnvironment* env)
{
	auto cmd = StringFormat(_T("%s"), args[1].AsString());
	PRINTF(StringFormat("AMTExec: %s\n", cmd).c_str());
	StdRedirectedSubProcess proc(cmd);
	proc.join();
	return args[0];
}

class AMTOrderedParallel : public GenericVideoFilter
{
	struct ClipData {
		PClip clip;
		int numFrames;
		int current;
		std::mutex mutex;
	};
	std::vector<ClipData> clips_;
public:
	AMTOrderedParallel(AVSValue clips, IScriptEnvironment* env)
		: GenericVideoFilter(clips[0].AsClip())
		, clips_(clips.ArraySize())
	{
		int maxFrames = 0;
		for (int i = 0; i < clips.ArraySize(); ++i) {
			clips_[i].clip = clips[i].AsClip();
			clips_[i].numFrames = clips_[i].clip->GetVideoInfo().num_frames;
			maxFrames = std::max(maxFrames, clips_[i].numFrames);
		}
		vi.num_frames = maxFrames * clips.ArraySize();
	}

	PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env)
	{
		int nclips = (int)clips_.size();
		int clipidx = n % nclips;
		auto& data = clips_[clipidx];
		int frameidx = std::min(data.numFrames - 1, n / nclips);
		std::unique_lock<std::mutex> lock(data.mutex);
		for (; data.current <= frameidx; data.current++) {
			data.clip->GetFrame(data.current, env);
		}
		return env->NewVideoFrame(vi);
	}

	int __stdcall SetCacheHints(int cachehints, int frame_range) {
		if (cachehints == CACHE_GET_MTMODE) {
			return MT_NICE_FILTER;
		}
		return 0;
	};

	static AVSValue __cdecl Create(AVSValue args, void* user_data, IScriptEnvironment* env)
	{
		return new AMTOrderedParallel(
			args[0],       // clips
			env
		);
	}
};
