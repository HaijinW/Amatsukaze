// chapter_exe.cpp : �R���\�[�� �A�v���P�[�V�����̃G���g�� �|�C���g���`���܂��B
//

#include "stdafx.h"
#include "source.h"
#include "faw.h"

// mvec.c
#define FRAME_PICTURE	1
#define FIELD_PICTURE	2
int mvec(int *mvec2,int *flag_sc,unsigned char* current_pix,unsigned char* bef_pix,int lx,int ly,int threshold,int pict_struct);

void write_chapter(FILE *f, int nchap, int frame, TCHAR *title, INPUT_INFO *iip) {
	LONGLONG t,h,m;
	double s;

	t = (LONGLONG)frame * 10000000 * iip->scale / iip->rate;
	h = t / 36000000000;
	m = (t - h * 36000000000) / 600000000;
	s = (t - h * 36000000000 - m * 600000000) / 10000000.0;

	fprintf(f, "CHAPTER%02d=%02d:%02d:%06.3f\n", nchap, (int)h, (int)m, s);
	fprintf(f, "CHAPTER%02dNAME=%s\n", nchap, title);
	fflush(f);
}

int _tmain(int argc, _TCHAR* argv[])
{
	// ���������[�N�`�F�b�N
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

	printf(_T("chapter.auf pre loading program.\n"));
	printf(_T("usage:\n"));
	printf(_T("\tchapter_exe.exe -v input_avs -o output_txt\n"));
	printf(_T("params:\n\t-v ���͉摜�t�@�C��\n\t-a ���͉����t�@�C���i�ȗ����͓���Ɠ����t�@�C���j\n\t-m ��������臒l�i1�`2^15)\n\t-s �Œᖳ���t���[����\n"));

	TCHAR *avsv = NULL;
	TCHAR *avsa = NULL;
	TCHAR *out =  NULL;
	short setmute = 50;
	int setseri = 10;

	for(int i=1; i<argc-1; i++) {
		char *s	= argv[i];
		if (s[0] == '-') {
			switch(s[1]) {
			case 'v':
				avsv = argv[i+1];
				if (strlen(s) > 2 && s[2] == 'a') {
					avsa = argv[i+1];
				}
				i++;
				break;
			case 'a':
				avsa = argv[i+1];
				i++;
				break;
			case 'o':
				out = argv[i+1];
				i++;
				break;
			case 'm':
				setmute = atoi(argv[i+1]);
				i++;
				break;
			case 's':
				setseri = atoi(argv[i+1]);
				i++;
				break;
			default:
				printf("error: unknown param: %s\n", s);
				break;
			}
		} else {
			printf("error: unknown param: %s\n", s);
		}
	}

	// �������͂������ꍇ�͓�����ɂ���Ɖ���
	if (avsa == NULL) {
		avsa = avsv;
	}

	if (out == NULL) {
		printf("error: no output file path!");
		return -1;
	}

	printf(_T("Setting\n"));
	printf(_T("\tvideo: %s\n\taudio: %s\n\tout: %s\n"), avsv, (strcmp(avsv, avsa) ? avsa : "(within video source)"), out);
	printf(_T("\tmute: %d\n\tseri: %d\n"), setmute, setseri);

	printf("Loading plugins.\n");

	Source *video = NULL;
	Source *audio = NULL;
	try {
		AuiSource *srcv = new AuiSource();
		srcv->init(avsv);
		if (srcv->has_video() == false) {
			srcv->release();
			throw "Error: No Video Found!";
		}
		video = srcv;
		// �����\�[�X�̏ꍇ�͓����C���X�^���X�œǂݍ���
		if (strcmp(avsv, avsa) == 0 && srcv->has_audio()) {
			audio = srcv;
			audio->add_ref();
		}

		// �������ʃt�@�C���̎�
		if (audio == NULL) {
			if (strlen(avsa) > 4 && _stricmp(".wav", avsa + strlen(avsa) - 4) == 0) {
				// wav
				WavSource *wav = new WavSource();
				wav->init(avsa);
				if (wav->has_audio()) {
					audio = wav;
					audio->set_rate(video->get_input_info().rate, video->get_input_info().scale);
				} else {
					wav->release();
				}
			} else {
				// aui
				AuiSource *aud = new AuiSource();
				aud->init(avsa);
				if (aud->has_audio()) {
					audio = aud;
					audio->set_rate(video->get_input_info().rate, video->get_input_info().scale);
				} else {
					aud->release();
				}
			}
		}

		if (audio == NULL) {
			throw "Error: No Audio!";
		}
	} catch(char *s) {
		if (video) {
			video->release();
		}
		printf("%s\n", s);
		return -1;
	}

	FILE *fout;
	if (fopen_s(&fout, out, "w") != 0) {
		printf("Error: output file open failed.");
		video->release();
		audio->release();
		return -1;
	}

	INPUT_INFO &vii = video->get_input_info();
	INPUT_INFO &aii = audio->get_input_info();

	printf(_T("Movie data\n"));
	printf(_T("\tVideo Frames: %d [%.02ffps]\n"), vii.n, (double)vii.rate / vii.scale);
	DWORD fcc = vii.handler;
	printf(_T("\tVideo Format: %c%c%c%c\n"), fcc & 0xFF, fcc >> 8 & 0xFF, fcc >> 16 & 0xFF, fcc >> 24);

	printf(_T("\tAudio Samples: %d [%dHz]\n"), aii.audio_n, aii.audio_format->nSamplesPerSec);

	if (fcc == 0x32424752 || fcc == 0x38344359) {
		printf(_T("Error: Unsupported color RGB/YC48."));
	}

	if (fcc != 0x32595559) {
		printf(_T("warning: only YUY2 is supported. continues...\n"));
		//return -1;
	}

	short buf[4800*2]; // 10fps�ȏ�
	int n = vii.n;

	// FAW check
	do {
		CFAW cfaw;
		int faws = 0;

		for (int i=0; i<min(90, n); i++) {
			int naudio = audio->read_audio(i, buf);
			int j = cfaw.findFAW(buf, naudio);
			if (j != -1) {
				cfaw.decodeFAW(buf+j, naudio-j, buf); // test decode
				faws++;
			}
		}
		if (faws > 5) {
			if (cfaw.isLoadFailed()) {
				printf("  Error: FAW detected, but no FAWPreview.auf.\n");
			} else {
				printf("  FAW detected.\n");
				audio = new FAWDecoder(audio);
			}
		}
	} while(0);

	printf(_T("--------\nStart searching...\n"));

	short mute = setmute;
	int seri = 0;
	int idx = 1;
	int frames[500];

	// start searching
	for (int i=0; i<n; i++) {
		// searching foward frame
		if (seri == 0) {
			int naudio = audio->read_audio(i+setseri-1, buf);
			naudio *= aii.audio_format->nChannels;

			bool skip = false;
			for (int j=0; j<naudio; ++j) {
				if (abs(buf[j]) > mute) {
					skip = true;
					break;
				}
			}
			if (skip) {
				i += setseri;
			}
		}

		bool nomute = false;
		int naudio = audio->read_audio(i, buf);
		naudio *= aii.audio_format->nChannels;

		for (int j=0; j<naudio; ++j) {
			if (abs(buf[j]) > mute) {
				nomute = true;
				break;
			}
		}
		if (nomute) {
			// owata
			if (seri >= setseri) {
				int start_fr = i - seri;

				printf(_T("mute%2d: %d - %d�t���[��\n"), idx, start_fr, seri);

				int w = vii.format->biWidth & 0xFFFFFFF0;
				int h = vii.format->biHeight & 0xFFFFFFF0;
				unsigned char *pix0 = (unsigned char*)_aligned_malloc(1920*1088, 32);
				unsigned char *pix1 = (unsigned char*)_aligned_malloc(1920*1088, 32);

				//--- ����������� by Yobi ---
				int last_fr = start_fr - 1;
				if (last_fr < 0){
					last_fr = 0;
				}
				video->read_video_y8(last_fr, pix0);

				int max_pos;
				int cmvec2;						// �C���^�[���[�X�̓������Ȃ����擾�p
				int flag_sc;					// �V�[���`�F���W����t���O
				int flag_sc_hold = 0;			// �ێ��V�[���`�F���W����t���O
				int last_cmvec  = 0;			// �O�t���[���̓������L���p
				int last_cmvec2 = 0;			// �O�t���[���̃C���^�[���[�X�p�������L���p
				int cnt_change = 0;				// �V�[���`�F���W�ʒu�ێ�����͎��̕ێ��܂ŊԊu���J���邽�߂̃J�E���^
				int msel = 0;					// ���Ԗڂ̃V�[���`�F���W���i0-1�j
				int max_msel;					// �ő�̃V�[���`�F���W�I��
				int d_max_en[2] = {0, 0};		// �V�[���`�F���W�̗L����
				int d_max_pos[2];				// �V�[���`�F���W�n�_�t���[���ԍ�
				int d_max_mvec[2];				// �V�[���`�F���W�n�_�������
				int d_maxp_mvec[2];				// �V�[���`�F���W�P�t���[���O�������
				int d_maxn_mvec[2];				// �V�[���`�F���W�P�t���[���㓮�����
				int d_max_mvec2[2];				// �V�[���`�F���W�n�_�C���^�[���[�X�p�������
				int d_maxp_mvec2[2];			// �V�[���`�F���W�P�t���[���O�C���^�[���[�X�p�������
				int d_maxn_mvec2[2];			// �V�[���`�F���W�P�t���[����C���^�[���[�X�p�������

				for (int x=start_fr; x<min(i, start_fr+300); x++) {
					video->read_video_y8(x, pix1);
					int cmvec = mvec( &cmvec2, &flag_sc, pix1, pix0, w, h, (100-0)*(100/FIELD_PICTURE), FIELD_PICTURE);
					if (d_max_en[msel] > 0){
						if (x == d_max_pos[msel]+1){			// �V�[���`�F���W�P�t���[����̓������X�V
							d_maxn_mvec[msel]  = cmvec;
							d_maxn_mvec2[msel] = cmvec2;
						}
					}
					if (flag_sc_hold > 0 && msel < 1){			// �V�[���`�F���W���o�؂�ւ��n�_
						msel ++;
						flag_sc_hold = 0;
						cnt_change = 3;
					}
					if (cnt_change > 0){		// �V�[���`�F���W���o�؂�ւ�����͘A���ŕێ����Ȃ��悤�Ԋu��������
						cnt_change --;
					}
					else{
						if (flag_sc > 0){			// �V�[���`�F���W����
							flag_sc_hold = 1;
						}
						if (d_max_mvec[msel] < cmvec || d_max_en[msel] == 0) {	// �V�[���`�F���W�n�_�X�V
							d_max_en[msel]     = 1;
							d_max_pos[msel]    = x;
							d_max_mvec[msel]   = cmvec;
							d_maxp_mvec[msel]  = last_cmvec;
							d_maxn_mvec[msel]  = 0;
							d_max_mvec2[msel]  = cmvec2;
							d_maxp_mvec2[msel] = last_cmvec2;
							d_maxn_mvec2[msel] = 0;
						}
					}
					unsigned char *tmp = pix0;
					pix0 = pix1;
					pix1 = tmp;
					last_cmvec  = cmvec;
					last_cmvec2 = cmvec2;
//					if (x>=9265 && x<=9269){
//						fprintf(fout, "(%d:%d)",x,cmvec);
//					}
				}
				// �Q�ӏ��ڈȍ~�ŃV�[���`�F���W���Ȃ������疳����
				if (flag_sc_hold == 0 && msel > 0){
					d_max_en[msel] = 0;
				}
				// �ő�V�[���`�F���W��max_msel�ɓ���Ă���
				if (d_max_en[1] != 0 && d_max_mvec[0] < d_max_mvec[1]){
					max_msel = 1;
				}
				else{
					max_msel = 0;
				}

				// add for searching last frame before changing scene
				// �O���㔼���ꂼ��V�[���`�F���W�O��̃t���[���ԍ����擾�i�C���^�[���[�X�Б��ω������O���j
				int d_maxpre_pos[2];		// �V�[���`�F���W�O
				int d_maxrev_pos[2];		// �V�[���`�F���W��
				for(int k=0; k<2; k++){
					if (d_max_en[k] > 0){
						d_maxpre_pos[k] = d_max_pos[k] - 1;		// �ʏ�͂P�t���[���O���V�[���`�F���W�O
						if (d_max_mvec[k] < d_maxp_mvec[k] * 2 && d_maxp_mvec[k] > d_maxp_mvec2[k] * 2){
							d_maxpre_pos[k] = d_max_pos[k] - 2;
						}
						d_maxrev_pos[k] = d_max_pos[k];			// �ʏ�̓V�[���`�F���W�n�_���V�[���`�F���W��
						if (d_max_mvec[k] > d_max_mvec2[k] * 2 &&
							d_max_mvec[k] < d_maxn_mvec[k] * 2 && d_maxn_mvec[k] > d_maxn_mvec2[k] * 2){
							d_maxrev_pos[k] = d_max_pos[k] + 1;
						}
						if (d_maxpre_pos[k] < 0){
							d_maxpre_pos[k] = 0;
						}
						if (d_maxrev_pos[k] < 0){
							d_maxrev_pos[k] = 0;
						}
					}
				}

				max_pos = d_max_pos[max_msel];
				frames[idx] = max_pos;
				for(int k=0; k<2; k++){
					char *mark = "";
					if (d_max_en[k] == 0) continue;		// �V�[���`�F���W��₩��O�ꂽ�ꍇ����

					if (k == max_msel){
						if (idx > 1 && abs(max_pos - frames[idx-1] - 30*15) < 30) {
							mark = "��";
						} else if (idx > 1 && abs(max_pos - frames[idx-1] - 30*30) < 30) {
							mark = "����";
						} else if (idx > 1 && abs(max_pos - frames[idx-1] - 30*45) < 30) {
							mark = "������";
						} else if (idx > 1 && abs(max_pos - frames[idx-1] - 30*60) < 30) {
							mark = "��������";
						}
					}
					else{	// ������ԓ��ő�2���V�[���`�F���W
							mark = "��";
					}
					printf("\t SCPos: %d %s\n", d_max_pos[k], mark);

					TCHAR title[256];
					sprintf_s(title, _T("%d�t���[�� %s SCPos:%d %d"), seri, mark, d_maxrev_pos[k], d_maxpre_pos[k]);
					if (0){		// for debug
						TCHAR tmp_title[256];
						sprintf_s(tmp_title, _T(" : %d [%d %d] [%d %d] [%d %d]"), i, d_maxn_mvec[k], d_maxn_mvec2[k], d_max_mvec[k], d_max_mvec2[k], d_maxp_mvec[k], d_maxp_mvec2[k]);
						strcat(title, tmp_title);
					}
					write_chapter(fout, idx, i-seri, title, &vii);
				}
				//--- �����܂ŉ��� by Yobi ---
				idx++;

				_aligned_free(pix0);
				_aligned_free(pix1);
			}
			seri = 0;
		} else {
			seri++;
		}
	}

	// �ŏI�t���[���ԍ����o�́i�����łŒǉ��j
	fprintf(fout, "# SCPos:%d %d\n", n-1, n-1);

	// �\�[�X�����
	video->release();
	audio->release();

	return 0;
}
