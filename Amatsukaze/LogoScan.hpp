#pragma once

#include "TranscodeManager.hpp"

#include "logo.h"

namespace logo {

static bool approxim_line(int n, double sum_x, double sum_y, double sum_x2, double sum_xy, double& a, double& b)
{
	// 0�ł̏��Z���
	double temp = (double)n * sum_x2 - sum_x * sum_x;
	if (temp == 0.0) return false;

	a = ((double)n * sum_xy - sum_x * sum_y) / temp;
	b = (sum_x2 * sum_y - sum_x * sum_xy) / temp;

	return true;
}

class LogoColor
{
	double sumF, sumB, sumF2, sumB2, sumFB;
public:
	LogoColor()
		: sumF()
		, sumB()
		, sumF2()
		, sumB2()
		, sumFB()
	{ }

	// �s�N�Z���̐F��ǉ� f:�O�i b:�w�i
	void Add(int f, int b)
	{
		sumF += f;
		sumB += b;
		sumF2 += f * f;
		sumB2 += b * b;
		sumFB += f * b;
	}

	/*====================================================================
	* 	GetAB_?()
	* 		��A�����̌X���ƐؕЂ�Ԃ�
	*===================================================================*/
	bool GetAB(double& A, double& B, int data_count) const {
		double A1, A2;
		double B1, B2;
		// XY����ւ������̗����ŕ��ς����
		// �w�i��X��
		if (false == approxim_line(data_count, sumF, sumB, sumF2, sumFB, A1, B1)
			|| false == approxim_line(data_count, sumB, sumF, sumB2, sumFB, A2, B2)) {
			return false;
		}

		A = (A1 + (1 / A2)) / 2;   // �X���𕽋�
		B = (B1 + (-B2 / A2)) / 2; // �ؕЂ�����

		return true;
	}
};

class LogoScan
{
	int scanw;
	int scanh;
	int logUXx;
	int logUVy;
	int thy;

	std::vector<uint8_t> tmpY, tmpU, tmpV;

	int nframes;
	std::unique_ptr<LogoColor[]> logoY, logoU, logoV;

	/*--------------------------------------------------------------------
	*	�^����ւ�𕽋�
	*-------------------------------------------------------------------*/
	int med_average(std::vector<uint8_t> s)
	{
		double t = 0;
		int nn = 0;

		int n = (int)s.size();

		// �^����ւ�𕽋�
		for (int i = n / 4; i < n - (n / 4); i++, nn++)
			t += s[i];

		t = (t + nn / 2) / nn;

		return ((int)t);
	}

	/*====================================================================
	* 	GetPixelAndBG()
	* 		LOGO_PIXEL��Ԃ�
	*===================================================================*/
	static void GetPixelAndBG(short *pixel, short *bg, double A, double B) {
		if (A == 1) {	// 0�ł̏��Z���
			*pixel = *bg = 0;
		}
		else {
			double temp = B / (1 - A) * 4 + 0.5; // *4�̓f�[�^��8bit�Ȃ̂�10bit�Ɋg�����Ă�����
			if (std::abs(temp) < 0x7FFF) {
				// short�͈͓̔�
				*pixel = (short)temp;
				temp = ((double)1 - A) * LOGO_MAX_DP + 0.5;
				if (std::abs(temp)>0x3FFF || short(temp) == 0) {
					*pixel = *bg = 0;
				}
				else {
					*bg = (short)temp;
				}
			}
			else {
				*pixel = *bg = 0;
			}
		}
	}

	/*====================================================================
	* 	GetLGP()
	* 		LOGO_PIXEL��Ԃ�
	*===================================================================*/
	bool GetLogoPixel(LOGO_PIXEL& lgp, LogoColor& Y, LogoColor& U, LogoColor& V) const
	{
		double A_Y, B_Y, A_Cb, B_Cb, A_Cr, B_Cr;
		if (Y.GetAB(A_Y, B_Y, nframes)
			&& U.GetAB(A_Cb, B_Cb, nframes)
			&& V.GetAB(A_Cr, B_Cr, nframes)) {
			GetPixelAndBG(&lgp.y, &lgp.dp_y, A_Y, B_Y);
			GetPixelAndBG(&lgp.cb, &lgp.dp_cb, A_Cb, B_Cb);
			GetPixelAndBG(&lgp.cr, &lgp.dp_cr, A_Cr, B_Cr);
			return true;
		}
		return false;
	}

public:
	// thy: �I���W�i�����ƃf�t�H���g30*8=240�i8bit����12���炢�H�j
	LogoScan(int scanw, int scanh, int logUXx, int logUVy, int thy)
		: scanw(scanw)
		, scanh(scanh)
		, logUXx(logUXx)
		, logUVy(logUVy)
		, thy(thy)
		, nframes()
	{
	}

	bool GetLogo(LOGO_PIXEL* dst) const
	{
		int scanUVw = scanw >> logUXx;
		int scanUVh = scanh >> logUVy;

		for (int y = 0; y < scanh; ++y) {
			for (int x = 0; x < scanw; ++x) {
				int off = x + y * scanw;
				int offUV = (x >> logUXx) + (y >> logUVy) * scanUVw;
				// �ʓ|�Ȃ̂�UV�͕�Ԃ��Ȃ�
				if (GetLogoPixel(dst[off], logoY[off], logoY[offUV], logoY[offUV]) == false)
				{
					return false;
				}
			}
		}

		return  true;
	}

	void AddScanFrame(
		const uint8_t* srcY, 
		const uint8_t* srcU,
		const uint8_t* srcV,
		int pitchY, int pitchUV,
		int bgY, int bgU, int bgV)
	{
		int scanUVw = scanw >> logUXx;
		int scanUVh = scanh >> logUVy;

		for (int y = 0; y < scanh; ++y) {
			for (int x = 0; x < scanw; ++x) {
				logoY[x + y * scanw].Add(srcY[x + y * pitchY], bgY);
			}
		}
		for (int y = 0; y < scanUVh; ++y) {
			for (int x = 0; x < scanUVw; ++x) {
				logoU[x + y * scanUVw].Add(srcU[x + y * pitchUV], bgU);
				logoV[x + y * scanUVw].Add(srcV[x + y * pitchUV], bgV);
			}
		}

		++nframes;
	}

	bool AddFrame(
		const uint8_t* srcY,
		const uint8_t* srcU,
		const uint8_t* srcV,
		int pitchY, int pitchUV)
	{
		int scanUVw = scanw >> logUXx;
		int scanUVh = scanh >> logUVy;

		tmpY.clear();
		tmpU.clear();
		tmpV.clear();

		tmpY.reserve((scanw + scanh - 1) * 2);
		tmpU.reserve((scanUVw + scanUVh - 1) * 2);
		tmpV.reserve((scanUVw + scanUVh - 1) * 2);

		/*--------------------------------------------------------------------
		*	�w�i�F�v�Z
		*-------------------------------------------------------------------*/

		for (int x = 0; x < scanw; ++x) {
			tmpY.push_back(srcY[x]);
			tmpY.push_back(srcY[x + (scanh - 1) * pitchY]);
		}
		for (int y = 1; y < scanh - 1; ++y) {
			tmpY.push_back(srcY[y * pitchY]);
			tmpY.push_back(srcY[scanw - 1 + y * pitchY]);
		}
		for (int x = 0; x < scanUVw; ++x) {
			tmpU.push_back(srcU[x]);
			tmpU.push_back(srcU[x + (scanUVh - 1) * pitchUV]);
			tmpV.push_back(srcV[x]);
			tmpV.push_back(srcV[x + (scanUVh - 1) * pitchUV]);
		}
		for (int y = 1; y < scanUVh - 1; ++y) {
			tmpU.push_back(srcU[y * pitchY]);
			tmpU.push_back(srcU[scanUVw - 1 + y * pitchUV]);
			tmpV.push_back(srcV[y * pitchY]);
			tmpV.push_back(srcV[scanUVw - 1 + y * pitchUV]);
		}

		// �ŏ��ƍő傪臒l�ȏ㗣��Ă���ꍇ�A�P��F�łȂ��Ɣ��f
		std::sort(tmpY.begin(), tmpY.end());
		if (abs(tmpY.front() - tmpY.back()) > thy) { // �I���W�i������ thy * 8
			return false;
		}
		std::sort(tmpU.begin(), tmpU.end());
		if (abs(tmpU.front() - tmpU.back()) > thy) { // �I���W�i������ thy * 8
			return false;
		}
		std::sort(tmpV.begin(), tmpV.end());
		if (abs(tmpV.front() - tmpV.back()) > thy) { // �I���W�i������ thy * 8
			return false;
		}

		int bgY = med_average(tmpY);
		int bgU = med_average(tmpU);
		int bgV = med_average(tmpV);

		// �L���t���[����ǉ�
		AddScanFrame(srcY, srcU, srcV, pitchY, pitchUV, bgY, bgU, bgV);

		return true;
	}

	bool AddFrame(PVideoFrame& frame)
	{
		const uint8_t* srcY = reinterpret_cast<const uint8_t*>(frame->GetReadPtr(PLANAR_Y));
		const uint8_t* srcU = reinterpret_cast<const uint8_t*>(frame->GetReadPtr(PLANAR_U));
		const uint8_t* srcV = reinterpret_cast<const uint8_t*>(frame->GetReadPtr(PLANAR_V));
		int pitchY = frame->GetPitch(PLANAR_Y) / sizeof(uint8_t);
		int pitchUV = frame->GetPitch(PLANAR_U) / sizeof(uint8_t);

		return AddFrame(srcY, srcU, srcV, pitchY, pitchUV);
	}
};

int ScanLogo(AMTContext& ctx, const TranscoderSetting& setting)
{
	const int scanw = 200;
	const int scanh = 100;

	std::string modulepath = GetModulePath();
	auto env = make_unique_ptr(CreateScriptEnvironment2());
	auto codec = make_unique_ptr(CCodec::CreateInstance(UTVF_ULH0, "Amatsukaze"));

	AVSValue result;
	if (env->LoadPlugin(modulepath.c_str(), false, &result) == false) {
		THROW(RuntimeException, "Failed to LoadPlugin ...");
	}

	PClip clip = env->Invoke("Import", setting.getFilterScriptPath().c_str(), 0).AsClip();
	VideoInfo vi = clip->GetVideoInfo();

	size_t scanDataSize = scanw * scanh * 3 / 2;
	size_t codedSize = codec->EncodeGetOutputSize(UTVF_YV12, scanw, scanh);
	size_t extraSize = codec->EncodeGetExtraDataSize();
	auto memScanData = std::unique_ptr<uint8_t[]>(new uint8_t[scanDataSize]);
	auto memCoded = std::unique_ptr<uint8_t[]>(new uint8_t[codedSize]);
	std::vector<uint8_t> extra(extraSize);

	if (codec->EncodeGetExtraData(extra.data(), extraSize, UTVF_YV12, scanw, scanh)) {
		THROW(RuntimeException, "failed to EncodeGetExtraData (UtVideo)");
	}
	if (codec->EncodeBegin(UTVF_YV12, scanw, scanh, CBGROSSWIDTH_WINDOWS)) {
		THROW(RuntimeException, "failed to EncodeBegin (UtVideo)");
	}

	LosslessVideoFile file(ctx, setting.getLogoTmpFilePath(), "wb");
	LogoScan logoscan(scanw, scanh,
		vi.GetPlaneWidthSubsampling(PLANAR_U),
		vi.GetPlaneHeightSubsampling(PLANAR_U),
		12);

	// �t���[�����͍ő�t���[�����i���ۂ͂����܂ŏ������܂Ȃ��j
	file.writeHeader(scanw, scanh, vi.num_frames, extra);

	int nValidFrames = 0;
	for (int i = 0; i < vi.num_frames; ++i) {
		PVideoFrame frame = clip->GetFrame(i, env.get());

		// �X�L������������
		PVideoFrame scanFrame = env->SubframePlanar(frame,
			vi.width - scanw, frame->GetPitch(PLANAR_Y), scanw, scanh,
			(vi.width - scanw) >> 1, (vi.width - scanw) >> 1, frame->GetPitch(PLANAR_U));

		if (logoscan.AddFrame(scanFrame)) {
			++nValidFrames;

			// �L���ȃt���[���͕ۑ����Ă���
			CopyYV12(memScanData.get(), frame, scanw, scanh);
			bool keyFrame = false;
			size_t codedSize = codec->EncodeFrame(memCoded.get(), &keyFrame, memScanData.get());
			file.writeFrame(memCoded.get(), (int)codedSize);
		}
	}

	codec->EncodeEnd();

	std::vector<LOGO_PIXEL> logodata(scanw * scanh);
	if (logoscan.GetLogo(logodata.data()) == false) {
		THROW(RuntimeException, "Insufficient logo frames");
	}

	// �L���t���[���f�[�^�Ə������S�̎擾�͊���
	// TODO: �f�[�^��͂ƃ��S�̍�蒼��

	return 0;
}

} // namespace logo
