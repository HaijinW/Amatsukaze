#pragma once

#include "TranscodeSetting.hpp"
#include "logo.h"
#include "AMTLogo.hpp"

#include <cmath>

namespace logo {

static void approxim_line(int n, double sum_x, double sum_y, double sum_x2, double sum_xy, double& a, double& b)
{
  // double��float�ɂ�NaN����`����Ă���̂Ń[�����Z�ŗ�O�͔������Ȃ�
	double temp = (double)n * sum_x2 - sum_x * sum_x;
	a = ((double)n * sum_xy - sum_x * sum_y) / temp;
	b = (sum_x2 * sum_y - sum_x * sum_xy) / temp;
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

  // �l��0�`1�ɐ��K��
  void Normalize(int maxv)
  {
    sumF /= (double)maxv;
    sumB /= (double)maxv;
    sumF2 /= (double)maxv*maxv;
    sumB2 /= (double)maxv*maxv;
    sumFB /= (double)maxv*maxv;
  }

	/*====================================================================
	* 	GetAB_?()
	* 		��A�����̌X���ƐؕЂ�Ԃ� X��:�O�i Y��:�w�i
	*===================================================================*/
	bool GetAB(float& A, float& B, int data_count) const
  {
		double A1, A2;
		double B1, B2;
    approxim_line(data_count, sumF, sumB, sumF2, sumFB, A1, B1);
    approxim_line(data_count, sumB, sumF, sumB2, sumFB, A2, B2);

    // XY����ւ������̗����ŕ��ς����
		A = (float)((A1 + (1 / A2)) / 2);   // �X���𕽋�
		B = (float)((B1 + (-B2 / A2)) / 2); // �ؕЂ�����

    if (std::isnan(A) || std::isnan(B) || std::isinf(A) || std::isinf(B) || A == 0)
      return false;

		return true;
	}
};

class LogoScan
{
	int scanw;
	int scanh;
	int logUVx;
	int logUVy;
	int thy;

	std::vector<short> tmpY, tmpU, tmpV;

	int nframes;
	std::unique_ptr<LogoColor[]> logoY, logoU, logoV;

	/*--------------------------------------------------------------------
	*	�^����ւ�𕽋�
	*-------------------------------------------------------------------*/
	int med_average(const std::vector<short>& s)
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

	static float calcDist(float a, float b) {
		return (1.0f / 3.0f) * (a - 1) * (a - 1) + (a - 1) * b + b * b;
	}

	static void maxfilter(float *data, float *work, int w, int h)
	{
		for (int y = 0; y < h; ++y) {
			work[0 + y * w] = data[0 + y * w];
			for (int x = 1; x < w - 1; ++x) {
				float a = data[x - 1 + y * w];
				float b = data[x + y * w];
				float c = data[x + 1 + y * w];
				work[x + y * w] = std::max(a, std::max(b, c));
			}
			work[w - 1 + y * w] = data[w - 1 + y * w];
		}
		for (int y = 1; y < h - 1; ++y) {
			for (int x = 0; x < w; ++x) {
				float a = data[x + (y - 1) * w];
				float b = data[x + y * w];
				float c = data[x + (y + 1) * w];
				work[x + y * w] = std::max(a, std::max(b, c));
			}
		}
	}

public:
	// thy: �I���W�i�����ƃf�t�H���g30*8=240�i8bit����12���炢�H�j
	LogoScan(int scanw, int scanh, int logUVx, int logUVy, int thy)
		: scanw(scanw)
		, scanh(scanh)
		, logUVx(logUVx)
		, logUVy(logUVy)
		, thy(thy)
		, nframes()
		, logoY(new LogoColor[scanw*scanh])
		, logoU(new LogoColor[scanw*scanh >> (logUVx + logUVy)])
		, logoV(new LogoColor[scanw*scanh >> (logUVx + logUVy)])
	{
	}

	void Normalize(int mavx)
	{
		int scanUVw = scanw >> logUVx;
		int scanUVh = scanh >> logUVy;

		// 8bit�Ȃ̂�255
		for (int y = 0; y < scanh; ++y) {
			for (int x = 0; x < scanw; ++x) {
				logoY[x + y * scanw].Normalize(mavx);
			}
		}
		for (int y = 0; y < scanUVh; ++y) {
			for (int x = 0; x < scanUVw; ++x) {
				logoU[x + y * scanUVw].Normalize(mavx);
				logoV[x + y * scanUVw].Normalize(mavx);
			}
		}
	}

	std::unique_ptr<LogoData> GetLogo() const
	{
		int scanUVw = scanw >> logUVx;
		int scanUVh = scanh >> logUVy;
    auto data = std::unique_ptr<LogoData>(new LogoData(scanw, scanh, logUVx, logUVy));
    float *aY = data->GetA(PLANAR_Y);
    float *aU = data->GetA(PLANAR_U);
    float *aV = data->GetA(PLANAR_V);
    float *bY = data->GetB(PLANAR_Y);
    float *bU = data->GetB(PLANAR_U);
    float *bV = data->GetB(PLANAR_V);

		for (int y = 0; y < scanh; ++y) {
			for (int x = 0; x < scanw; ++x) {
				int off = x + y * scanw;
        if (!logoY[off].GetAB(aY[off], bY[off], nframes)) return nullptr;
			}
		}
    for (int y = 0; y < scanUVh; ++y) {
      for (int x = 0; x < scanUVw; ++x) {
        int off = x + y * scanUVw;
        if (!logoU[off].GetAB(aU[off], bU[off], nframes)) return nullptr;
        if (!logoV[off].GetAB(aV[off], bV[off], nframes)) return nullptr;
      }
    }

		// ���S���Y��ɂ���
		int sizeY = scanw * scanh;
		auto dist = std::unique_ptr<float[]>(new float[sizeY]());
		for (int y = 0; y < scanh; ++y) {
			for (int x = 0; x < scanw; ++x) {
				int off = x + y * scanw;
				int offUV = (x >> logUVx) + (y >> logUVy) * scanUVw;
				dist[off] = calcDist(aY[off], bY[off]) + 
					calcDist(aU[offUV], bU[offUV]) +
					calcDist(aV[offUV], bV[offUV]);

				// �l�����������ĕ�����ɂ����̂ő傫�����Ă�����
				dist[off] *= 1000;
			}
		}

		// max�t�B���^���|����
		auto work = std::unique_ptr<float[]>(new float[sizeY]);
		maxfilter(dist.get(), work.get(), scanw, scanh);
		maxfilter(dist.get(), work.get(), scanw, scanh);
		maxfilter(dist.get(), work.get(), scanw, scanh);

		// �������Ƃ���̓[���ɂ���
		for (int y = 0; y < scanh; ++y) {
			for (int x = 0; x < scanw; ++x) {
				int off = x + y * scanw;
				int offUV = (x >> logUVx) + (y >> logUVy) * scanUVw;
				if (dist[off] < 0.3f) {
					aY[off] = 1;
					bY[off] = 0;
					aU[offUV] = 1;
					bU[offUV] = 0;
					aV[offUV] = 1;
					bV[offUV] = 0;
				}
			}
		}

		return  data;
	}

	template <typename pixel_t>
	void AddScanFrame(
		const pixel_t* srcY,
		const pixel_t* srcU,
		const pixel_t* srcV,
		int pitchY, int pitchUV,
		int bgY, int bgU, int bgV)
	{
		int scanUVw = scanw >> logUVx;
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

	template <typename pixel_t>
	bool AddFrame(
		const pixel_t* srcY,
		const pixel_t* srcU,
		const pixel_t* srcV,
		int pitchY, int pitchUV)
	{
		int scanUVw = scanw >> logUVx;
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
			tmpU.push_back(srcU[y * pitchUV]);
			tmpU.push_back(srcU[scanUVw - 1 + y * pitchUV]);
			tmpV.push_back(srcV[y * pitchUV]);
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
};

class SimpleVideoReader : AMTObject
{
public:
	SimpleVideoReader(AMTContext& ctx)
		: AMTObject(ctx)
	{ }

	void readAll(const std::string& src)
	{
		using namespace av;

		InputContext inputCtx(src);
		if (avformat_find_stream_info(inputCtx(), NULL) < 0) {
			THROW(FormatException, "avformat_find_stream_info failed");
		}
		AVStream *videoStream = GetVideoStream(inputCtx());
		if (videoStream == NULL) {
			THROW(FormatException, "Could not find video stream ...");
		}
		AVCodecID vcodecId = videoStream->codecpar->codec_id;
		AVCodec *pCodec = avcodec_find_decoder(vcodecId);
		if (pCodec == NULL) {
			THROW(FormatException, "Could not find decoder ...");
		}
		CodecContext codecCtx(pCodec);
		if (avcodec_parameters_to_context(codecCtx(), videoStream->codecpar) != 0) {
			THROW(FormatException, "avcodec_parameters_to_context failed");
		}
		if (avcodec_open2(codecCtx(), pCodec, NULL) != 0) {
			THROW(FormatException, "avcodec_open2 failed");
		}

		bool first = true;
		Frame frame;
		AVPacket packet = AVPacket();
		while (av_read_frame(inputCtx(), &packet) == 0) {
			if (packet.stream_index == videoStream->index) {
				if (avcodec_send_packet(codecCtx(), &packet) != 0) {
					THROW(FormatException, "avcodec_send_packet failed");
				}
				while (avcodec_receive_frame(codecCtx(), frame()) == 0) {
					if (first) {
						onFirstFrame(videoStream, frame());
						first = false;
					}
					if (!onFrame(frame())) {
						av_packet_unref(&packet);
						return;
					}
				}
			}
			av_packet_unref(&packet);
		}

		// flush decoder
		if (avcodec_send_packet(codecCtx(), NULL) != 0) {
			THROW(FormatException, "avcodec_send_packet failed");
		}
		while (avcodec_receive_frame(codecCtx(), frame()) == 0) {
			onFrame(frame());
		}
	}

protected:
	virtual void onFirstFrame(AVStream *videoStream, AVFrame* frame) { };
	virtual bool onFrame(AVFrame* frame) { return true; };
};

static void CreateLogoMask(LogoData& dst, int w, int h)
{
	float *dstAY = dst.GetA(PLANAR_Y);
	float *dstBY = dst.GetB(PLANAR_Y);

	size_t YSize = w * h;
	auto memWork = std::unique_ptr<float[]>(new float[YSize]);

	dst.AllocateMask();
	uint8_t* dstMask = dst.GetMask(PLANAR_Y);
	memset(dstMask, 0, YSize);

	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			// x�ؕЁA�܂�A�w�i�̋P�x�l�[���̂Ƃ��̃��S�̋P�x�l���擾
			float a = dstAY[x + y * w];
			float b = dstBY[x + y * w];
			memWork[x + y * w] = -b / a;
		}
	}

	// 3x3 Prewitt
	for (int y = 1; y < h - 1; ++y) {
		for (int x = 1; x < w - 1; ++x) {
			const float* ptr = &memWork[x + y * w];
			float y_sum_h = 0, y_sum_v = 0;

			y_sum_h -= ptr[-1 + w * -1];
			y_sum_h -= ptr[-1];
			y_sum_h -= ptr[-1 + w * 1];
			y_sum_h += ptr[1 + w * -1];
			y_sum_h += ptr[1];
			y_sum_h += ptr[1 + w * 1];
			y_sum_v -= ptr[-1 + w * -1];
			y_sum_v -= ptr[0 + w * -1];
			y_sum_v -= ptr[1 + w * -1];
			y_sum_v += ptr[-1 + w * 1];
			y_sum_v += ptr[0 + w * 1];
			y_sum_v += ptr[1 + w * 1];

			float val = std::sqrtf(y_sum_h * y_sum_h + y_sum_v * y_sum_v);
			dstMask[x + y * w] = (val > 0.25f);
		}
	}
}

static void DeintLogo(LogoData& dst, LogoData& src, int w, int h)
{
	const float *srcAY = src.GetA(PLANAR_Y);
	float *dstAY = dst.GetA(PLANAR_Y);
	const float *srcBY = src.GetB(PLANAR_Y);
	float *dstBY = dst.GetB(PLANAR_Y);

	auto merge = [](float a, float b, float c) { return (a + 2 * b + c) / 4.0f; };

	for (int x = 0; x < w; ++x) {
		dstAY[x] = srcAY[x];
		dstBY[x] = srcBY[x];
		dstAY[x + (h - 1) * w] = srcAY[x + (h - 1) * w];
		dstBY[x + (h - 1) * w] = srcBY[x + (h - 1) * w];
	}
	for (int y = 1; y < h - 1; ++y) {
		for (int x = 0; x < w; ++x) {
			dstAY[x + y * w] = merge(
				srcAY[x + (y - 1) * w],
				srcAY[x + y * w],
				srcAY[x + (y + 1) * w]);
			dstBY[x + y * w] = merge(
				srcBY[x + (y - 1) * w],
				srcBY[x + y * w],
				srcBY[x + (y + 1) * w]);
		}
	}
}

template <typename pixel_t>
void DeintY(float* dst, const pixel_t* src, int srcPitch, int w, int h)
{
	auto merge = [](int a, int b, int c) { return (a + 2 * b + c + 2) / 4.0f; };

	for (int x = 0; x < w; ++x) {
		dst[x] = src[x];
		dst[x + (h - 1) * w] = src[x + (h - 1) * srcPitch];
	}
	for (int y = 1; y < h - 1; ++y) {
		for (int x = 0; x < w; ++x) {
			dst[x + y * w] = merge(
				src[x + (y - 1) * srcPitch],
				src[x + y * srcPitch],
				src[x + (y + 1) * srcPitch]);
		}
	}
}

static float EvaluateLogo(const float *src,float maxv, LogoData& logo, float fade, float* work, int w, int h)
{
	// ���S��]�� //
	const float *logoAY = logo.GetA(PLANAR_Y);
	const float *logoBY = logo.GetB(PLANAR_Y);
	const uint8_t* mask = logo.GetMask(PLANAR_Y);

	// ���S������
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			float srcv = src[x + y * w];
			float a = logoAY[x + y * w];
			float b = logoBY[x + y * w];
			float bg = a * srcv + b * maxv;
			work[x + y * w] = fade * bg + (1 - fade) * srcv;
		}
	}

	// �G�b�W�]��
	float result = 0;
	for (int y = 2; y < h - 2; ++y) {
		for (int x = 2; x < w - 2; ++x) {
			if (mask[x + y * w]) { // ���S�֊s���̂�
				float y_sum_h = 0, y_sum_v = 0;

				// 5x5 Prewitt filter
				// +----------------+  +----------------+
				// | -1 -1 -1 -1 -1 |  | -1 -1  0  1  1 |
				// | -1 -1 -1 -1 -1 |  | -1 -1  0  1  1 |
				// |  0  0  0  0  0 |  | -1 -1  0  1  1 |
				// |  1  1  1  1  1 |  | -1 -1  0  1  1 |
				// |  1  1  1  1  1 |  | -1 -1  0  1  1 |
				// +----------------+  +----------------+
				y_sum_h -= work[x - 2 + (y - 2) * w];
				y_sum_h -= work[x - 2 + (y - 1) * w];
				y_sum_h -= work[x - 2 + (y)* w];
				y_sum_h -= work[x - 2 + (y + 1) * w];
				y_sum_h -= work[x - 2 + (y + 2) * w];
				y_sum_h -= work[x - 1 + (y - 2) * w];
				y_sum_h -= work[x - 1 + (y - 1) * w];
				y_sum_h -= work[x - 1 + (y)* w];
				y_sum_h -= work[x - 1 + (y + 1) * w];
				y_sum_h -= work[x - 1 + (y + 2) * w];
				y_sum_h += work[x + 1 + (y - 2) * w];
				y_sum_h += work[x + 1 + (y - 1) * w];
				y_sum_h += work[x + 1 + (y)* w];
				y_sum_h += work[x + 1 + (y + 1) * w];
				y_sum_h += work[x + 1 + (y + 2) * w];
				y_sum_h += work[x + 2 + (y - 2) * w];
				y_sum_h += work[x + 2 + (y - 1) * w];
				y_sum_h += work[x + 2 + (y)* w];
				y_sum_h += work[x + 2 + (y + 1) * w];
				y_sum_h += work[x + 2 + (y + 2) * w];
				y_sum_v -= work[x - 2 + (y - 1) * w];
				y_sum_v -= work[x - 1 + (y - 1) * w];
				y_sum_v -= work[x + (y - 1) * w];
				y_sum_v -= work[x + 1 + (y - 1) * w];
				y_sum_v -= work[x + 2 + (y - 1) * w];
				y_sum_v -= work[x - 2 + (y - 2) * w];
				y_sum_v -= work[x - 1 + (y - 2) * w];
				y_sum_v -= work[x + (y - 2) * w];
				y_sum_v -= work[x + 1 + (y - 2) * w];
				y_sum_v -= work[x + 2 + (y - 2) * w];
				y_sum_v += work[x - 2 + (y + 1) * w];
				y_sum_v += work[x - 1 + (y + 1) * w];
				y_sum_v += work[x + (y + 1) * w];
				y_sum_v += work[x + 1 + (y + 1) * w];
				y_sum_v += work[x + 2 + (y + 1) * w];
				y_sum_v += work[x - 2 + (y + 2) * w];
				y_sum_v += work[x - 1 + (y + 2) * w];
				y_sum_v += work[x + (y + 2) * w];
				y_sum_v += work[x + 1 + (y + 2) * w];
				y_sum_v += work[x + 2 + (y + 2) * w];

				float val = std::sqrt(y_sum_h * y_sum_h + y_sum_v * y_sum_v);
				result += val;
			}
		}
	}

	return result;
}

class LogoAnalyzer : AMTObject
{
  const TranscoderSetting& setting_;

	int scanx, scany;
  int scanw, scanh, thy;
	int numMaxFrames;
  int logUVx, logUVy;
	int imgw, imgh;
  int numFrames;
  std::unique_ptr<LogoData> logodata;

	// ���̏��t���k��8bit�݂̂Ȃ̂őΉ���8bit�̂�
	class InitialLogoCreator : SimpleVideoReader
	{
		LogoAnalyzer* pThis;
		CCodecPointer codec;
		size_t scanDataSize;
		size_t codedSize;
		int readCount;
		std::unique_ptr<uint8_t[]> memScanData;
		std::unique_ptr<uint8_t[]> memCoded;
		std::unique_ptr<LosslessVideoFile> file;
		std::unique_ptr<LogoScan> logoscan;
	public:
		InitialLogoCreator(LogoAnalyzer* pThis)
			: SimpleVideoReader(pThis->ctx)
			, pThis(pThis)
			, codec(make_unique_ptr(CCodec::CreateInstance(UTVF_ULH0, "Amatsukaze")))
			, scanDataSize(pThis->scanw * pThis->scanh * 3 / 2)
			, codedSize(codec->EncodeGetOutputSize(UTVF_YV12, pThis->scanw, pThis->scanh))
			, readCount()
			, memScanData(new uint8_t[scanDataSize])
			, memCoded(new uint8_t[codedSize])
		{ }
		void readAll(const std::string& src)
		{
			SimpleVideoReader::readAll(src);

			codec->EncodeEnd();

			logoscan->Normalize(255);
			pThis->logodata = logoscan->GetLogo();
			if (pThis->logodata == nullptr) {
				THROW(RuntimeException, "Insufficient logo frames");
			}
		}
	protected:
		virtual void onFirstFrame(AVStream *videoStream, AVFrame* frame)
		{
			size_t extraSize = codec->EncodeGetExtraDataSize();
			std::vector<uint8_t> extra(extraSize);

			if (codec->EncodeGetExtraData(extra.data(), extraSize, UTVF_YV12, pThis->scanw, pThis->scanh)) {
				THROW(RuntimeException, "failed to EncodeGetExtraData (UtVideo)");
			}
			if (codec->EncodeBegin(UTVF_YV12, pThis->scanw, pThis->scanh, CBGROSSWIDTH_WINDOWS)) {
				THROW(RuntimeException, "failed to EncodeBegin (UtVideo)");
			}

			const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get((AVPixelFormat)(frame->format));
			
			pThis->logUVx = desc->log2_chroma_w;
			pThis->logUVy = desc->log2_chroma_h;
			pThis->imgw = frame->width;
			pThis->imgh = frame->height;

			file = std::unique_ptr<LosslessVideoFile>(
				new LosslessVideoFile(pThis->ctx, pThis->setting_.getLogoTmpFilePath(), "wb"));
			logoscan = std::unique_ptr<LogoScan>(
				new LogoScan(pThis->scanw, pThis->scanh, pThis->logUVx, pThis->logUVy, pThis->thy));

			// �t���[�����͍ő�t���[�����i���ۂ͂����܂ŏ������܂Ȃ����Ƃ�����j
			file->writeHeader(pThis->scanw, pThis->scanh, pThis->numMaxFrames, extra);

			pThis->numFrames = 0;
		};
		virtual bool onFrame(AVFrame* frame)
		{
			readCount++;

			if (pThis->numFrames >= pThis->numMaxFrames) return false;

			// �X�L������������
			int pitchY = frame->linesize[0];
			int pitchUV = frame->linesize[1];
			int offY = pThis->scanx + pThis->scany * pitchY;
			int offUV = (pThis->scanx >> pThis->logUVx) + (pThis->scany >> pThis->logUVy) * pitchUV;
			const uint8_t* scanY = frame->data[0] + offY;
			const uint8_t* scanU = frame->data[1] + offUV;
			const uint8_t* scanV = frame->data[2] + offUV;

			if (logoscan->AddFrame(scanY, scanU, scanV, pitchY, pitchUV)) {
				++pThis->numFrames;

				// �L���ȃt���[���͕ۑ����Ă���
				CopyYV12(memScanData.get(), scanY, scanU, scanV, pitchY, pitchUV, pThis->scanw, pThis->scanh);
				bool keyFrame = false;
				size_t codedSize = codec->EncodeFrame(memCoded.get(), &keyFrame, memScanData.get());
				file->writeFrame(memCoded.get(), (int)codedSize);
			}

			if ((readCount % 2000) == 0) printf("%d frames\n", readCount);

			return true;
		};
	};

  void MakeInitialLogo()
  {
		InitialLogoCreator creator(this);
		creator.readAll(setting_.getSrcFilePath());
  }


  void ReMakeLogo()
  {
    // ����fade�l�Ń��S��]�� //
		auto codec = make_unique_ptr(CCodec::CreateInstance(UTVF_ULH0, "Amatsukaze"));

    // ���S��]���p�ɃC���^������
    LogoData deintLogo(scanw, scanh, logUVx, logUVy);
    DeintLogo(deintLogo, *logodata, scanw, scanh);
		CreateLogoMask(deintLogo, scanw, scanh);

		size_t scanDataSize = scanw * scanh * 3 / 2;
		size_t YSize = scanw * scanh;
		size_t codedSize = codec->EncodeGetOutputSize(UTVF_YV12, scanw, scanh);
		size_t extraSize = codec->EncodeGetExtraDataSize();
		auto memScanData = std::unique_ptr<uint8_t[]>(new uint8_t[scanDataSize]);
		auto memCoded = std::unique_ptr<uint8_t[]>(new uint8_t[codedSize]);

		auto memDeint = std::unique_ptr<float[]>(new float[YSize]);
		auto memWork = std::unique_ptr<float[]>(new float[YSize]);

		const int numFade = 20;
		auto minFades = std::unique_ptr<int[]>(new int[numFrames]);
		{
			LosslessVideoFile file(ctx, setting_.getLogoTmpFilePath(), "rb");
			file.readHeader();
			auto extra = file.getExtra();

			if (codec->DecodeBegin(UTVF_YV12, scanw, scanh, CBGROSSWIDTH_WINDOWS, extra.data(), (int)extra.size())) {
				THROW(RuntimeException, "failed to DecodeBegin (UtVideo)");
			}

			// �S�t���[�����[�v
			for (int i = 0; i < numFrames; ++i) {
				int64_t codedSize = file.readFrame(i, memCoded.get());
				if (codec->DecodeFrame(memScanData.get(), memCoded.get()) != scanDataSize) {
					THROW(RuntimeException, "failed to DecodeFrame (UtVideo)");
				}
				// �t���[�����C���^������
				DeintY(memDeint.get(), memScanData.get(), scanw, scanw, scanh);
				// fade�l���[�v
				float minResult = FLT_MAX;
				int minFadeIndex = 0;
				for (int fi = 0; fi < numFade; ++fi) {
					float fade = 0.1f * fi;
					// ���S��]��
					float result = EvaluateLogo(memDeint.get(), 255.0f, deintLogo, fade, memWork.get(), scanw, scanh);
					if (result < minResult) {
						minResult = result;
						minFadeIndex = fi;
					}
				}
				minFades[i] = minFadeIndex;

				if ((i % 2000) == 0) printf("%d frames\n", i);
			}

			codec->DecodeEnd();
		}

    // �]���l���W��
		// �Ƃ肠�����o���Ă݂�
		std::vector<int> numMinFades(numFade);
		for (int i = 0; i < numFrames; ++i) {
			numMinFades[minFades[i]]++;
		}
		int maxi = (int)(std::max_element(numMinFades.begin(), numMinFades.end()) - numMinFades.begin());
		printf("maxi = %d (%.1f%%)\n", maxi, numMinFades[maxi] / (float)numFrames * 100.0f);

		LogoScan logoscan(scanw, scanh, logUVx, logUVy, thy);
		{
			LosslessVideoFile file(ctx, setting_.getLogoTmpFilePath(), "rb");
			file.readHeader();
			auto extra = file.getExtra();

			if (codec->DecodeBegin(UTVF_YV12, scanw, scanh, CBGROSSWIDTH_WINDOWS, extra.data(), (int)extra.size())) {
				THROW(RuntimeException, "failed to DecodeBegin (UtVideo)");
			}

			int scanUVw = scanw >> logUVx;
			int scanUVh = scanh >> logUVy;
			int offU = scanw * scanh;
			int offV = offU + scanUVw * scanUVh;

			// �S�t���[�����[�v
			for (int i = 0; i < numFrames; ++i) {
				int64_t codedSize = file.readFrame(i, memCoded.get());
				if (codec->DecodeFrame(memScanData.get(), memCoded.get()) != scanDataSize) {
					THROW(RuntimeException, "failed to DecodeFrame (UtVideo)");
				}
				// ���S�̂���t���[������AddFrame
				if (minFades[i] > 8) { // TODO: ����
					const uint8_t* ptr = memScanData.get();
					logoscan.AddFrame(ptr, ptr + offU, ptr + offV, scanw, scanUVw);
				}

				if ((i % 2000) == 0) printf("%d frames\n", i);
			}

			codec->DecodeEnd();
		}

    // ���S�쐬
		logoscan.Normalize(255);
		logodata = logoscan.GetLogo();
		if (logodata == nullptr) {
			THROW(RuntimeException, "Insufficient logo frames");
		}
  }

public:
  LogoAnalyzer(AMTContext& ctx, const TranscoderSetting& setting)
    : AMTObject(ctx)
    , setting_(setting)
  {
    //
  }

  int ScanLogo()
  {
    sscanf(setting_.getModeArgs().c_str(), "%d,%d,%d,%d,%d,%d",
      &scanx, &scany, &scanw, &scanh, &thy, &numMaxFrames);

		// �L���t���[���f�[�^�Ə������S�̎擾
    MakeInitialLogo();

    // �f�[�^��͂ƃ��S�̍�蒼��
		ReMakeLogo();
		ReMakeLogo();
		//ReMakeLogo();

		LogoHeader header(scanw, scanh, logUVx, logUVy, imgw, imgh, scanx, scany, "No Name");
		logodata->Save(setting_.getOutFilePath(0) + ".lgd", &header);

    return 0;
  }
};

int ScanLogo(AMTContext& ctx, const TranscoderSetting& setting)
{
  LogoAnalyzer analyzer(ctx, setting);
  return analyzer.ScanLogo();
}

// ������T�� //

template <typename Op, bool shortHead>
float GoldenRatioSearch(float x0, float x1, float x2, float v0, float v1, float v2, Op& op)
{
	assert(x0 < x1);
	assert(x1 < x2);

	if (op.end(x0, x1, x2, v0, v1, v2)) {
		return x1;
	}

	float x3 = x0 + (x2 - x1);
	float v3 = op.f(x3);

	if (shortHead) {
		// ���̋�Ԃ̂ق�������
		if (v3 < v1) {
			// ���̋��
			return GoldenRatioSearch<Op, true>(x1, x3, x2, v1, v3, v2, op);
		}
		else {
			// �O�̋��
			return GoldenRatioSearch<Op, false>(x0, x1, x3, v0, v1, v3, op);
		}
	}
	else {
		// �O�̋�Ԃ̂ق�������
		if (v3 > v1) {
			// ���̋��
			return GoldenRatioSearch<Op, true>(x3, x1, x2, v3, v1, v2, op);
		}
		else {
			// �O�̋��
			return GoldenRatioSearch<Op, false>(x0, x3, x1, v0, v3, v1, op);
		}
	}
}

template <typename Op>
float GoldenRatioSearch(float x0, float x1, Op& op)
{
	assert(x0 < x1);

	// ���[
	float v0 = op.f(x0);
	float v1 = op.f(x1);

	// �����_�i������ŕ����j
	float x2 = x0 + (x1 - x0) * 2 / (3 + std::sqrtf(5.0f));
	float v2 = op.f(x2);

	return GoldenRatioSearch<Op, true>(x0, x2, x1, v0, v2, v1, op);
}

class AMTEraseLogo : public GenericVideoFilter
{
	std::unique_ptr<LogoData> logo;
	std::unique_ptr<LogoData> deintLogo;
	LogoHeader header;
	float thresh;

	template <typename pixel_t>
	void Delogo(pixel_t* dst, int w, int h, int pitch, float maxv, const float* A, const float* B, float fade)
	{
		for (int y = 0; y < h; ++y) {
			for (int x = 0; x < w; ++x) {
				float srcv = dst[x + y * pitch];
				float a = A[x + y * w];
				float b = B[x + y * w];
				float bg = a * srcv + b * maxv;
				float tmp = fade * bg + (1 - fade) * srcv;
				dst[x + y * pitch] = (pixel_t)std::min(std::max(tmp + 0.5f, 0.0f), maxv);
			}
		}
	}

	template <typename pixel_t>
	PVideoFrame GetFrameT(int n, IScriptEnvironment2* env)
	{
		size_t YSize = header.w * header.h;
		auto memDeint = std::unique_ptr<float[]>(new float[YSize]);
		auto memWork = std::unique_ptr<float[]>(new float[YSize]);

		PVideoFrame frame = child->GetFrame(n, env);
		env->MakeWritable(&frame);

		float maxv = (float)((1 << vi.BitsPerComponent()) - 1);
		pixel_t* dstY = reinterpret_cast<pixel_t*>(frame->GetWritePtr(PLANAR_Y));
		pixel_t* dstU = reinterpret_cast<pixel_t*>(frame->GetWritePtr(PLANAR_U));
		pixel_t* dstV = reinterpret_cast<pixel_t*>(frame->GetWritePtr(PLANAR_V));

		int pitchY = frame->GetPitch(PLANAR_Y);
		int pitchUV = frame->GetPitch(PLANAR_U);
		int off = header.imgx + header.imgy * pitchY;
		int offUV = (header.imgx >> header.logUVx) + (header.imgy >> header.logUVy) * pitchUV;

		// �t���[�����C���^������
		DeintY(memDeint.get(), dstY + off, pitchY, header.w, header.h);

		// Fade�l�T��
		struct SearchOp {
			float* img;
			float* work;
			int w, h;
			float maxv;
			LogoData& logo;
			SearchOp(float* img, float* work, float maxv, LogoData& logo, int w, int h)
				: img(img), work(work), maxv(maxv), logo(logo), w(w), h(h) { }
			bool end(float x0, float x1, float x2, float v0, float v1, float v2) {
				return (x2 - x0) < 0.01f;
			}
			float f(float x) {
				return EvaluateLogo(img, maxv, logo, x, work, w, h);
			}
		} op(memDeint.get(), memWork.get(), maxv, *deintLogo, header.w, header.h);

		// �܂��A�S�̂�����
		float minResult = FLT_MAX;
		float minFade = 0;
		for (int fi = 0; fi < 13; ++fi) {
			float fade = 0.1f * fi;
			// ���S��]��
			float result = op.f(fade);
			if (result < minResult) {
				minResult = result;
				minFade = fade;
			}
		}

		// �ŏ��l�t�߂��ׂ�������
		float optimalFade = GoldenRatioSearch(minFade - 0.1f, minFade + 0.1f, op);

		DebugPrint("Fade: %.1f\n", optimalFade * 100.0f);

		if (optimalFade > thresh) optimalFade = 1.0f;
		
		// �œKFade�l�Ń��S����
		const float *logoAY = logo->GetA(PLANAR_Y);
		const float *logoBY = logo->GetB(PLANAR_Y);
		const float *logoAU = logo->GetA(PLANAR_U);
		const float *logoBU = logo->GetB(PLANAR_U);
		const float *logoAV = logo->GetA(PLANAR_V);
		const float *logoBV = logo->GetB(PLANAR_V);

		int wUV = (header.w >> header.logUVx);
		int hUV = (header.h >> header.logUVy);

		Delogo(dstY + off, header.w, header.h, pitchY, maxv, logoAY, logoBY, optimalFade);
		Delogo(dstU + offUV, wUV, hUV, pitchUV, maxv, logoAU, logoBU, optimalFade);
		Delogo(dstV + offUV, wUV, hUV, pitchUV, maxv, logoAV, logoBV, optimalFade);

		return frame;
	}

public:
	AMTEraseLogo(PClip clip, const std::string& logoPath, float thresh, IScriptEnvironment* env)
		: GenericVideoFilter(clip)
		, thresh(thresh)
	{
		try {
			logo = LogoData::Load(logoPath, &header);
		}
		catch (IOException&) {
			env->ThrowError("Failed to read logo file (%s)", logoPath.c_str());
		}

		deintLogo = std::unique_ptr<LogoData>(
			new LogoData(header.w, header.h, header.logUVx, header.logUVy));
		DeintLogo(*deintLogo, *logo, header.w, header.h);
		deintLogo->AllocateMask();
		CreateLogoMask(*deintLogo, header.w, header.h);
	}

	PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env_)
	{
		IScriptEnvironment2* env = static_cast<IScriptEnvironment2*>(env_);

		int pixelSize = vi.ComponentSize();
		switch (pixelSize) {
		case 1:
			return GetFrameT<uint8_t>(n, env);
		case 2:
			return GetFrameT<uint16_t>(n, env);
		default:
			env->ThrowError("[AMTEraseLogo] Unsupported pixel format");
		}

		return PVideoFrame();
	}

	static AVSValue __cdecl Create(AVSValue args, void* user_data, IScriptEnvironment* env)
	{
		return new AMTEraseLogo(
			args[0].AsClip(),       // source
			args[1].AsString(),			// logopath
			(float)args[2].AsFloat(2),			// thresh
			env
		);
	}
};

} // namespace logo
