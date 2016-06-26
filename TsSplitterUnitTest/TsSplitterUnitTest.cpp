// TsSplitterUnitTest.cpp : �R���\�[�� �A�v���P�[�V�����̃G���g�� �|�C���g���`���܂��B
//
#define _CRT_SECURE_NO_WARNINGS

#include <string>

#include "gtest/gtest.h"

#include "TsSplitter.cpp"

#include "WaveWriter.h"

// FAAD
#include "faad.h"
#pragma comment(lib, "libfaad2.lib")

// �Ȃ�����`����Ă��Ȃ��̂�
namespace std {
	int abs(int a, int b) {
		return (a > b) ? (a - b) : (b - a);
	}
}

// �e�X�gINI�t�@�C���A�N�Z�X
static std::string getTestFileParam(const char* key) {
	char buf[200];
	if (!GetPrivateProfileStringA("FILE", key, "", buf, sizeof(buf), "TestParam.ini")) {
		return "";
	}
	return std::string(buf);
}

static bool fileExists(const char* filepath) {
	WIN32_FIND_DATAA findData;
	HANDLE hFind = FindFirstFileA(filepath, &findData);
	if (hFind == INVALID_HANDLE_VALUE) {
		return false;
	}
	FindClose(hFind);
	return true;
}

// �e�X�g�ΏۂƂȂ�N���X Foo �̂��߂̃t�B�N�X�`��
class FooTest : public ::testing::Test {
protected:
	// �ȍ~�̊֐��Œ��g�̂Ȃ����͎̂��R�ɍ폜�ł��܂��D
	//

	FooTest() {
		// �e�X�g���Ɏ��s����� set-up �������ɏ����܂��D
	}

	virtual ~FooTest() {
		// �e�X�g���Ɏ��s�����C��O�𓊂��Ȃ� clean-up �������ɏ����܂��D
	}

	// �R���X�g���N�^�ƃf�X�g���N�^�ł͕s�\���ȏꍇ�D
	// �ȉ��̃��\�b�h���`���邱�Ƃ��ł��܂��F

	virtual void SetUp() {
		// ���̃R�[�h�́C�R���X�g���N�^�̒���i�e�e�X�g�̒��O�j
		// �ɌĂяo����܂��D
	}

	virtual void TearDown() {
		// ���̃R�[�h�́C�e�e�X�g�̒���i�f�X�g���N�^�̒��O�j
		// �ɌĂяo����܂��D
	}

	// �����Ő錾�����I�u�W�F�N�g�́C�e�X�g�P�[�X���̑S�Ẵe�X�g�ŗ��p�ł��܂��D
};

// Abc ���s�� Foo::Bar() ���\�b�h���e�X�g���܂��D
TEST_F(FooTest, MethodBarDoesAbc) {
	//EXPECT_EQ(0, f.Bar(input_filepath, output_filepath));
}

// Xyz ���s�� Foo ���e�X�g���܂��D
TEST_F(FooTest, DoesXyz) {
	// Foo �� Xyz ������
}

TEST(CRC, PrintTable) {

	CRC32 crc;

	const uint32_t* table = crc.getTable();

	for (int i = 0; i < 256; ++i) {
		printf("0x%08x%c", table[i], ((i+1) % 8) ? ',' : '\n');
	}
}

static void toBytesBE(uint8_t* bytes, uint32_t data) {
	bytes[3] = ((data >> 0) & 0xFF);
	bytes[2] = ((data >> 8) & 0xFF);
	bytes[1] = ((data >> 16) & 0xFF);
	bytes[0] = ((data >> 24) & 0xFF);
}

TEST(CRC, CheckCRC) {

	CRC32 crc;

	const uint8_t* data = (const uint8_t*)"ABCD";
	uint32_t result = crc.calc(data, 4, 123456);
	// printf("RESULT: 0x%x\n");
	uint8_t buf[4]; toBytesBE(buf, result);
	uint32_t t = crc.calc(data, 4, 123456);
	result = crc.calc(buf, 4, t);
	EXPECT_EQ(result, 0);
}

TEST(Util, readOpt) {
	uint8_t data[16];
	srand(0);
	for (int i = 0; i < sizeof(data); ++i) data[i] = rand();

	//uint16_t a = read16(data);
	//uint32_t b = read24(data);
	//uint32_t c = read32(data);
	//uint64_t d = read40(data);
	uint64_t e = read48(data);

	printf("sum=%f\n", double(e));
}

void VerifyMpeg2Ps(std::string srcfile) {
	enum {
		BUF_SIZE = 1024 * 1024 * 1024, // 1GB
	};
	uint8_t* buf = (uint8_t*)malloc(BUF_SIZE); // 
	FILE* fp = fopen(srcfile.c_str(), "rb");
	try {
		TsSplitterContext ctx;
		PsStreamVerifier psVerifier(&ctx);

		size_t readBytes = fread(buf, 1, BUF_SIZE, fp);
		psVerifier.verify(MemoryChunk(buf, readBytes));
	}
	catch (Exception e) {
		printf("Verify MPEG2-PS Error: ��O���X���[����܂��� -> %s\n", e.message());
		ADD_FAILURE();
	}
	free(buf);
	buf = NULL;
	fclose(fp);
	fp = NULL;
}

void ParserTest(const std::string& filename) {
	std::string srcDir = getTestFileParam("TestDataDir") + "\\";
	std::string dstDir = getTestFileParam("TestWorkDir") + "\\";

	std::string srcfile = srcDir + filename + ".ts";
	std::string mpgfile = dstDir + filename + ".mpg";
	std::string aacfile = dstDir + filename + ".aac";
	std::string wavfile = dstDir + filename + ".wav";

	if (filename.size() == 0 || !fileExists(srcfile.c_str())) {
		printf("�e�X�g�t�@�C�����Ȃ��̂ŃX�L�b�v: %s\n", srcfile.c_str());
		return;
	}

	FILE* srcfp = fopen(srcfile.c_str(), "rb");
	FILE* mpgfp = fopen(mpgfile.c_str(), "wb");
	FILE* aacfp = fopen(aacfile.c_str(), "wb");
	FILE* wavfp = fopen(wavfile.c_str(), "wb");
	ASSERT_TRUE(srcfp != NULL);
	ASSERT_TRUE(mpgfp != NULL);
	ASSERT_TRUE(aacfp != NULL);
	ASSERT_TRUE(wavfp != NULL);

	TsSplitterContext ctx;
	TsSplitter tsSplitter(&ctx);
	tsSplitter.mpgfp = mpgfp;
	tsSplitter.aacfp = aacfp;
	tsSplitter.wavfp = wavfp;

	int bufsize = 16 * 1024;
	uint8_t* buffer = (uint8_t*)malloc(bufsize);

	while (1) {
		int readsize = (int)fread(buffer, 1, bufsize, srcfp);
		if (readsize == 0) break;
		tsSplitter.inputTsData(MemoryChunk(buffer, readsize));
	}

	tsSplitter.flush();
	tsSplitter.printInteraceCount();

	free(buffer);
	fclose(srcfp);
	fclose(mpgfp);
	fclose(aacfp);
	fclose(wavfp);

	// �o�̓t�@�C�����`�F�b�N
	VerifyMpeg2Ps(mpgfile);
}

TEST(Input, MPEG2Parser) {
	ParserTest(getTestFileParam("MPEG2VideoTsFile"));
}

TEST(Input, H264Parser) {
	ParserTest(getTestFileParam("H264VideoTsFile"));
}

TEST(Input, H264Parser1Seg) {
	ParserTest(getTestFileParam("1SegVideoTsFile"));
}

TEST(Input, MPEG2PSVerifier) {
	std::string srcDir = getTestFileParam("TestDataDir") + "\\";
	std::string filename = getTestFileParam("SampleMPEG2PsFile");
	std::string srcfile = srcDir + filename + ".mpg";
	VerifyMpeg2Ps(srcfile);
}

TEST(Input, MPEG2PSWriter) {
	std::string srcDir = getTestFileParam("TestDataDir") + "\\";
	std::string filename = getTestFileParam("SampleMPEG2PsFile");
	std::string srcfile = srcDir + filename + ".mpg";

	enum {
		BUF_SIZE = 1024 * 1024 * 1024, // 1GB
	};
	uint8_t* buf = (uint8_t*)malloc(BUF_SIZE); // 
	FILE* fp = fopen(srcfile.c_str(), "rb");
	try {
		TsSplitterContext ctx;
		PsStreamVerifier psVerifier(&ctx);

		size_t readBytes = fread(buf, 1, BUF_SIZE, fp);
		psVerifier.verify(MemoryChunk(buf, readBytes));
	}
	catch (Exception) {
		free(buf);
		buf = NULL;
		fclose(fp);
		fp = NULL;
	}
}

TEST(Input, AutoBufferTest) {

	srand(0);

	uint8_t *buf = new uint8_t[65536];
	int addCnt = 0;
	int delCnt = 0;

	AutoBuffer ab;
	for (int i = 0; i < 10000; ++i) {
		int addNum = rand();
		int delNum = rand();
		
		for (int c = 0; c < addNum; ++c) {
			buf[c] = addCnt++;
		}
		//printf("Add %d\n", addNum);
		ab.add(buf, addNum);

		uint8_t *data = ab.get();
		for (int c = 0; c < ab.size(); ++c) {
			if (data[c] != ((delCnt + c) & 0xFF)) {
				ASSERT_TRUE(false);
			}
		}

		delNum = std::min<int>(delNum, (int)ab.size());
		//printf("Del %d\n", delNum);
		ab.trimHead(delNum);
		delCnt += delNum;
	}
	delete buf;
}

// FAAD�f�R�[�h���������o�͂����邩�e�X�g
TEST(FAAD, DecodeVerifyTest) {
	std::string srcDir = getTestFileParam("TestDataDir") + "\\";
	std::string filename = getTestFileParam("SampleAACFile");
	std::string srcfile = srcDir + filename + ".aac";
	std::string testfile = srcDir + filename + ".wav";

	if (filename.size() == 0 || !fileExists(srcfile.c_str())) {
		printf("�e�X�g�t�@�C�����Ȃ��̂ŃX�L�b�v: %s\n", srcfile.c_str());
		return;
	}
	if (!fileExists(testfile.c_str())) {
		printf("�e�X�g�t�@�C�����Ȃ��̂ŃX�L�b�v: %s\n", testfile.c_str());
		return;
	}

	FILE* fp = fopen(srcfile.c_str(), "rb");

	enum {
		BUF_SIZE = 1024 * 1024, // 1MB
	};
	uint8_t* buf = (uint8_t*)malloc(BUF_SIZE);
	size_t readBytes = fread(buf, 1, BUF_SIZE, fp);

	AutoBuffer decoded;

	NeAACDecHandle hAacDec = NeAACDecOpen();
	/*
	NeAACDecConfigurationPtr conf = NeAACDecGetCurrentConfiguration(hAacDec);
	conf->outputFormat = FAAD_FMT_16BIT;
	NeAACDecSetConfiguration(hAacDec, conf);
	*/

	unsigned long samplerate;
	unsigned char channels;
	ASSERT_FALSE(NeAACDecInit(hAacDec, buf, (unsigned long)readBytes, &samplerate, &channels));

	printf("samplerate=%d, channels=%d\n", samplerate, channels);

	for (int i = 0; i < readBytes; ) {
		NeAACDecFrameInfo frameInfo;
		void* samples = NeAACDecDecode(hAacDec, &frameInfo, buf + i, (unsigned long)readBytes - i);
		decoded.add((uint8_t*)samples, frameInfo.samples * 2);
		i += frameInfo.bytesconsumed;
	}

	// �����f�[�^�Ɣ�r
	FILE* testfp = fopen(testfile.c_str(), "rb");
	uint8_t* testbuf = (uint8_t*)malloc(BUF_SIZE);
	size_t testBytes = fread(testbuf, 1, BUF_SIZE, testfp);
	// data chunk��T��
	for (int i = sizeof(RiffHeader); ; ) {
		ASSERT_TRUE(i < testBytes - 8);
		if (read32(testbuf + i) == 'data') {
			int testLength = (int)testBytes - i - 8;
			const uint16_t* pTest = (const uint16_t*)(testbuf + i + 8);
			const uint16_t* pDec = (const uint16_t*)decoded.get();
			EXPECT_TRUE(testLength == decoded.size());
			// AAC�̃f�R�[�h���ʂ͏����Ȃ̂Ŋۂߌ덷���l������
			for (int c = 0; c < testLength/2; ++c) {
				EXPECT_TRUE(std::abs(pTest[c], pDec[c]) <= 1);
			}
			break;
		}
		i += *(uint32_t*)(testbuf + i + 4) + 8;
	}

	NeAACDecClose(hAacDec);
	free(buf);
	free(testbuf);
	fclose(fp);
	fclose(testfp);
}

int main(int argc, char **argv)
{
	::testing::GTEST_FLAG(filter) = "*MPEG2Parser";
	::testing::InitGoogleTest(&argc, argv);
	int result = RUN_ALL_TESTS();

	getchar();

	return result;
}

