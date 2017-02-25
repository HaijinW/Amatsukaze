// IIR_3DNR�t�B���^  by H_Kasahara(aka.HK) ���q��
// �V�[���`�F���W���o�p�ɃA���S���Y�����C by Yobi


//---------------------------------------------------------------------
//		�������������p
//---------------------------------------------------------------------

#include "stdafx.h"
#include <Windows.h>
#include <stdlib.h>
#include <limits.h>
#include <stdio.h>

#define FRAME_PICTURE	1
#define FIELD_PICTURE	2
#define MAX_SEARCH_EXTENT 32	//�S�T���̍ő�T���͈́B+-���̒l�܂ŁB
#define RATE_SCENE_CHANGE 8		//�V�[���`�F���W�Ɣ��肷�銄����
#define THRES_STILLDATA   8     //�x�^�h��摜�p�Ɍ덷�͈͂Ɣ��f������K���Ȓl
#define THRES_CMPPIX      40	//�⑫�������s�p臒l

//---------------------------------------------------------------------
//		�֐���`
//---------------------------------------------------------------------
//void make_motion_lookup_table();
//BOOL mvec(unsigned char* current_pix,unsigned char* bef_pix,int* vx,int* vy,int lx,int ly,int threshold,int pict_struct,int SC_level);
int mvec(int *mvec2,int *flag_sc,unsigned char* current_pix,unsigned char* bef_pix,int lx,int ly,int threshold,int pict_struct);
int tree_search(unsigned char* current_pix,unsigned char* bef_pix,int lx,int ly,int *vx,int *vy,int search_block_x,int search_block_y,int min,int pict_struct, int method);
int full_search(unsigned char* current_pix,unsigned char* bef_pix,int lx,int ly,int *vx,int *vy,int search_block_x,int search_block_y,int min,int pict_struct, int search_extent);
int dist( unsigned char *p1, unsigned char *p2, int lx, int distlim, int block_hight );
int maxmin_block( unsigned char *p, int lx, int block_height );

//---------------------------------------------------------------------
//		�O���[�o���ϐ�
//---------------------------------------------------------------------
int	block_hight, lx2;


//---------------------------------------------------------------------
//		�����덷����֐�
//---------------------------------------------------------------------
//[ru] �����x�N�g���̍��v��Ԃ�
int tree=0, full=0;
int mvec( int *mvec2,					//�C���^�[���[�X�œ��������Ȃ����̌��ʂ��i�[�i�o�́j
          int *flag_sc,					//�V�[���`�F���W�t���O�i�o�́j
		  unsigned char* current_pix, 	//���t���[���̋P�x�B8�r�b�g�B
		  unsigned char* bef_pix,		//�O�t���[���̋P�x�B8�r�b�g�B
		  int lx,						//�摜�̉���
		  int ly,						//�摜�̏c��
		  int threshold,				//�������x�B(100-fp->track[1])*50 �c�c 50�͓K���Ȓl�B
		  int pict_struct)				//"1"�Ȃ�t���[�������A"2"�Ȃ�t�B�[���h����
{
	int x, y;
	unsigned char *p1, *p2;
	int motion_vector_total = 0;
	int calc_total_lane0, calc_total_lane1;
	int calc_total;
	int cnt_sc, cnt_scb, cnt_total;
	int rate_sc, b_sc, b_sc0, b_sc1;

//�֐����Ăяo�����Ɍv�Z�����ɂ��ނ悤�O���[�o���ϐ��Ƃ���
	lx2 = lx*pict_struct;
	block_hight = 16/pict_struct;

	for(int i=0;i<pict_struct;i++)
	{
		calc_total = 0;
		cnt_sc = 0;
		cnt_scb = 0;
		cnt_total = 0;
		for(y=i;y<ly;y+=16)	//�S�̏c��
		{
			p1 = current_pix + y*lx;
			p2 = bef_pix + y*lx;
			for(x=0;x<lx;x+=16)	//�S�̉���
			{
				int vx=0, vy=0;
				int method = 0;
				int min = dist( p1, p2, lx2, INT_MAX, block_hight );
				int minnew = min;		//��~���̓��������L���i�⑫�����p�j
				int vxnew = 0;			//�i�⑫�����p�j
				int vynew = 0;			//�i�⑫�����p�j
				int b_nodif = 0;		//�i�⑫�����p�j
				if (threshold >= min){	//�t���[���Ԃ̐�Βl�����ŏ����珬������Ίȗ���
					//method = 1;		//���������l���ɓ����Ȃ炱����
					method = 2;			//���x�D��Ȃ炱����
					b_nodif = 1;		//�����Ȃ����L���i�⑫�����p�j
				}
				if( threshold < (min = tree_search( p1, p2, lx, ly, &vx, &vy, x, y, min, pict_struct, method)))
//�t���[���Ԃ̐�Βl�����傫����ΑS�T���������Ȃ�
					if ( threshold < (min = full_search( p1, &p2[vy * lx + vx], lx, ly, &vx, &vy, x+vx, y+vy, min, pict_struct, max(abs(vx),abs(vy))*2 ))){
						vx = MAX_SEARCH_EXTENT * 10;		// ���o�ł��Ȃ������ꍇ�A�傫�Ȓl��ݒ�
						vy = MAX_SEARCH_EXTENT * 10;
						cnt_sc ++;
					}
//�����x�N�g���̍��v���V�[���`�F���W���x���𒴂��Ă�����A�V�[���`�F���W�Ɣ��肵�đ傫�Ȓl��ݒ�

				if (threshold >= min && b_nodif == 0){
//�i�⑫�����j��������Ɣ��肳�ꂽ�ꍇ�A���t���[���������󔒂ɋ߂��ꍇ�͑O�t���[���̓��������o
//�O�t���[���\�������������ꍇ�͒ʏ���@�ł͌��o�ł��Ȃ��P�[�X������⑫�Ƃ��Ēǉ�
					if ( maxmin_block(p1, lx2, block_hight) * 2 + THRES_CMPPIX <= maxmin_block(p2, lx2, block_hight) ){
						if( threshold < (minnew = tree_search( p2, p1, lx, ly, &vxnew, &vynew, x, y, minnew, pict_struct, method)))
							if ( threshold < (minnew = full_search( p2, &p1[vynew * lx + vxnew], lx, ly, &vxnew, &vynew, x+vxnew, y+vynew, minnew, pict_struct, max(abs(vxnew),abs(vynew))*2 ))){
								vx = MAX_SEARCH_EXTENT * 4;		// ���o�ł��Ȃ������ꍇ�A�⑫�O���͑傫�Ȓl��ݒ�
								vy = MAX_SEARCH_EXTENT * 4;
								cnt_scb ++;
							}
					}
				}

				calc_total += abs(vx)+abs(vy);

				p1+=16;
				p2+=16;
				cnt_total ++;
			}
		}
		// �V�[���`�F���W�̊������v�Z�i�⑫�������͉e�����Ȃ߂ɕ␳�j
		rate_sc = (cnt_sc + cnt_scb/2) * 100 / cnt_total;
		if (rate_sc >= RATE_SCENE_CHANGE){
			b_sc = 1;
		}
		else{
			b_sc = 0;
		}
		// �C���^�[���[�X�̓g�b�v�^�{�g���ŕʁX�ɋL���B���v�Z�Ƌ�ʂōŒ�P�ȏ�ɂ��邽�߂P�����Z
		if (i == 0){
			calc_total_lane0 = calc_total+1;
			b_sc0 = b_sc;
		}
		else{
			calc_total_lane1 = calc_total+1;
			b_sc1 = b_sc;
		}
	}

	if (pict_struct != 2){				// �t���[�������̏ꍇ�A���ʂ����̂܂ܑ��
		motion_vector_total = calc_total_lane0;
		*mvec2 = calc_total_lane0;
		*flag_sc = b_sc0;
	}
	else if (calc_total_lane0 >= calc_total_lane1){		// �C���^�[���[�X��lane0�̕��������傫����
		motion_vector_total = calc_total_lane0;
		*mvec2 = calc_total_lane1;
		*flag_sc = b_sc0;
	}
	else{												// �C���^�[���[�X��lane1�̕��������傫����
		motion_vector_total = calc_total_lane1;
		*mvec2 = calc_total_lane0;
		*flag_sc = b_sc1;
	}

	/*char str[500];
	sprintf_s(str, 500, "tree:%d, full:%d", tree, full);
	MessageBox(NULL, str, 0, 0);*/

	return motion_vector_total;
}
//---------------------------------------------------------------------
//		�ȈՒT���@���������֐�
//      �����l�̏ꍇ�͒��S�ɋ߂�����I������
//---------------------------------------------------------------------
int tree_search(unsigned char* current_pix,	//���t���[���̋P�x�B8�r�b�g�B
				unsigned char* bef_pix,		//�O�t���[���̋P�x�B8�r�b�g�B
				int lx,						//�摜�̉���
				int ly,						//�摜�̏c��
				int *vx,					//x�����̓����x�N�g������������B
				int *vy,					//y�����̓����x�N�g������������B
				int search_block_x,			//�����ʒu
				int search_block_y,			//�����ʒu
				int min,					//���ʒu�ł̃t���[���Ԃ̐�Βl���B�֐����ł͓��ʒu�̔�r�����Ȃ��̂ŁA�Ăяo���O�ɍs���K�v����B
				int pict_struct,			//"1"�Ȃ�t���[�������A"2"�Ȃ�t�B�[���h����
				int method)					//�����̊ȈՉ��i0:�T������ 1:�Q���T�� 2:�����ȗ��j
{
	tree++;
	int dx, dy, ddx=0, ddy=0, xs=0, ys;
	int d;
	int x,y;
	int locx, locy;
	int loopmax, inter;
	int nrep, step, dthres;
	int speedup = pict_struct-1;
//�����͈͂̏���Ɖ�����ݒ�
	int ylow  = 0 - search_block_y;
	int yhigh = ly- search_block_y-16;
	int xlow  = 0 - search_block_x;
	int xhigh = lx- search_block_x-16;

	if (method == 2) return min;	// �����ȗ�

	if (method == 0){
		loopmax = 3-speedup;
		inter = 0;					// inter�͕s�g�p
	}
	else{
		loopmax = 5-speedup;		// MAX_SEARCH_EXTENT=32�̎��i�v�Z�ȗ��̂��ߒ��ڒ�`�j
		inter = MAX_SEARCH_EXTENT;
	}
	for(int i=0; i<loopmax; i++){
		if (method == 0){			// �Q�i�K�Ō����i�t�B�[���h�����Ŕ�r���v�X�U��j
			if (i==0){
				locx = MAX_SEARCH_EXTENT - 8;
				locy = MAX_SEARCH_EXTENT - 8;
				nrep = MAX_SEARCH_EXTENT/8*2 - 1;
				step = 8;
				dthres = THRES_STILLDATA << 4;		// �덷�͈͂Ƃ���K���Ȓl
			}
			else if (i==1){
				locx = ddx - 6;
				locy = ddy - 6;
				nrep = 7;
				step = 2;
				dthres = THRES_STILLDATA << 2;		// �덷�͈͂Ƃ���K���Ȓl
			}
			else{
				locx = ddx - 1;
				locy = ddy - 1;
				nrep = 3;
				step = 1;
				dthres = 1;			// �덷�͈͂Ƃ���K���Ȓl
			}
		}
		else{						// �Q���T���i�t�B�[���h�����Ŕ�r���v�R�Q��j
			inter = inter / 2;
			locx = ddx - inter;
			locy = ddy - inter;
			nrep = 3;
			step = inter;
			dthres = THRES_STILLDATA << (loopmax - i - 1);		// �덷�͈͂Ƃ���K���Ȓl
		}
		// �����J�n
		dy = locy;
		for(y=0; y<nrep; y++){
			if ( dy<ylow || dy>yhigh ){			//�����ʒu����ʊO�ɏo�Ă����猟���������Ȃ�Ȃ��B
			}
			else{
				ys = dy * lx;	//�����ʒu�c��
				dx = locx;
				for(x=0; x<nrep; x++){
					if( dx<xlow || dx>xhigh ){	//�����ʒu����ʊO�ɏo�Ă����猟���������Ȃ�Ȃ��B
					}
					else if (x == (nrep-1)/2 && y == (nrep-1)/2){	// ���S���W�ł͌v�Z���Ȃ��B
					}
					else{
						d = dist( current_pix, &bef_pix[ys+dx], lx2, min, block_hight );
						if( d <= min ){	//����܂ł̌������t���[���Ԃ̐�Βl���������������炻�ꂼ�����B
							if ((d + dthres <= min) ||
								(abs(dx) + abs(dy) <= abs(ddx) - abs(ddy))){	// ���S�ɋ߂����A�덷臒l�ȏ㍷������ꍇ�Z�b�g
									min = d;
									ddx = dx;
									ddy = dy;
							}
						}
					}
					dx += step;
				}
			}
			dy += step;
		}
	}

	if(pict_struct==FIELD_PICTURE){
		for(x=0,dx=ddx-1;x<3;x+=2,dx+=2){
			if( search_block_x+dx<0 || search_block_x+dx+16>lx )	continue;	//�����ʒu����ʊO�ɏo�Ă����猟���������Ȃ�Ȃ��B
			d = dist( current_pix, &bef_pix[ys+dx], lx2, min, block_hight );
			if( d < min ){	//����܂ł̌������t���[���Ԃ̐�Βl���������������炻�ꂼ�����B
				min = d;
				ddx = dx;
			}
		}
	}
	

	*vx += ddx;
	*vy += ddy;

	return min;
}
//---------------------------------------------------------------------
//		�S�T���@���������֐�
//      �����l�̏ꍇ�͒��S�ɋ߂�����I������
//---------------------------------------------------------------------
int full_search(unsigned char* current_pix,	//���t���[���̋P�x�B8�r�b�g�B
				unsigned char* bef_pix,		//�O�t���[���̋P�x�B8�r�b�g�B
				int lx,						//�摜�̉���
				int ly,						//�摜�̏c��
				int *vx,					//x�����̓����x�N�g������������B
				int *vy,					//y�����̓����x�N�g������������B
				int search_block_x,			//�����ʒu
				int search_block_y,			//�����ʒu
				int min,					//�t���[���Ԃ̐�Βl���B�ŏ��̒T���ł�INT_MAX�������Ă���B
				int pict_struct,			//"1"�Ȃ�t���[�������A"2"�Ȃ�t�B�[���h����
				int search_extent)			//�T���͈́B
{
	full++;
	int dx, dy, ddx=0, ddy=0;
	int d;
	int dthres;
//	int search_point;
	unsigned char* p2;

	if(search_extent>MAX_SEARCH_EXTENT)
		search_extent = MAX_SEARCH_EXTENT;

//�����͈͂̏���Ɖ������摜����͂ݏo���Ă��Ȃ����`�F�b�N
	int ylow  = 0 - ( (search_block_y-search_extent<0) ? search_block_y : search_extent );
	int yhigh = (search_block_y+search_extent+16>ly) ? ly-search_block_y-16 : search_extent;
	int xlow  = 0 - ( (search_block_x-search_extent<0) ? search_block_x : search_extent );
	int xhigh = (search_block_x+search_extent+16>lx) ? lx-search_block_x-16 : search_extent;

	dthres = THRES_STILLDATA;		// �덷�͈͂Ƃ���K���Ȓl
	for(dy=ylow;dy<=yhigh;dy+=pict_struct)
	{
		p2 = bef_pix + dy*lx + xlow;	//Y�������ʒu�Bxlow�͕��̒l�Ȃ̂�"p2=bef_pix+dy*lx-xlow"�Ƃ͂Ȃ�Ȃ�
		for(dx=xlow;dx<=xhigh;dx++)
		{
			d = dist( current_pix, p2, lx2, min, block_hight );
			if(d <= min)	//����܂ł̌������t���[���Ԃ̐�Βl���������������炻�ꂼ�����B
			{
				if ((d + dthres <= min) ||
					(abs(dx) + abs(dy) <= abs(ddx) - abs(ddy))){	// ���S�ɋ߂����A�덷臒l�ȏ㍷������ꍇ�Z�b�g
					min = d;
					ddx = dx;
					ddy = dy;
				}
			}
			p2++;
		}
	}

	*vx += ddx;
	*vy += ddy;

	return min;
}
//---------------------------------------------------------------------
//		�t���[���Ԑ�Βl�����v�֐�
//---------------------------------------------------------------------
//bbMPEG�̃\�[�X�𗬗p
#include <emmintrin.h>

int dist( unsigned char *p1, unsigned char *p2, int lx, int distlim, int block_height )
{
	if (block_height == 8) {
		__m128i a, b, r;

		a = _mm_load_si128 ((__m128i*)p1 +  0);
		b = _mm_loadu_si128((__m128i*)p2 +  0);
		r = _mm_sad_epu8(a, b);

		a = _mm_load_si128 ((__m128i*)(p1 + lx));
		b = _mm_loadu_si128((__m128i*)(p2 + lx));
		r = _mm_add_epi32(r, _mm_sad_epu8(a, b));

		a = _mm_load_si128 ((__m128i*)(p1 + 2*lx));
		b = _mm_loadu_si128((__m128i*)(p2 + 2*lx));
		r = _mm_add_epi32(r, _mm_sad_epu8(a, b));

		a = _mm_load_si128 ((__m128i*)(p1 + 3*lx));
		b = _mm_loadu_si128((__m128i*)(p2 + 3*lx));
		r = _mm_add_epi32(r, _mm_sad_epu8(a, b));

		a = _mm_load_si128 ((__m128i*)(p1 + 4*lx));
		b = _mm_loadu_si128((__m128i*)(p2 + 4*lx));
		r = _mm_add_epi32(r, _mm_sad_epu8(a, b));

		a = _mm_load_si128 ((__m128i*)(p1 + 5*lx));
		b = _mm_loadu_si128((__m128i*)(p2 + 5*lx));
		r = _mm_add_epi32(r, _mm_sad_epu8(a, b));

		a = _mm_load_si128 ((__m128i*)(p1 + 6*lx));
		b = _mm_loadu_si128((__m128i*)(p2 + 6*lx));
		r = _mm_add_epi32(r, _mm_sad_epu8(a, b));

		a = _mm_load_si128 ((__m128i*)(p1 + 7*lx));
		b = _mm_loadu_si128((__m128i*)(p2 + 7*lx));
		r = _mm_add_epi32(r, _mm_sad_epu8(a, b));
		return _mm_extract_epi16(r, 0) + _mm_extract_epi16(r, 4);;
	}

	int s = 0;
	for(int i=0;i<block_height;i++)
	{
		/*
		s += motion_lookup[p1[0]][p2[0]];
		s += motion_lookup[p1[1]][p2[1]];
		s += motion_lookup[p1[2]][p2[2]];
		s += motion_lookup[p1[3]][p2[3]];
		s += motion_lookup[p1[4]][p2[4]];
		s += motion_lookup[p1[5]][p2[5]];
		s += motion_lookup[p1[6]][p2[6]];
		s += motion_lookup[p1[7]][p2[7]];
		s += motion_lookup[p1[8]][p2[8]];
		s += motion_lookup[p1[9]][p2[9]];
		s += motion_lookup[p1[10]][p2[10]];
		s += motion_lookup[p1[11]][p2[11]];
		s += motion_lookup[p1[12]][p2[12]];
		s += motion_lookup[p1[13]][p2[13]];
		s += motion_lookup[p1[14]][p2[14]];
		s += motion_lookup[p1[15]][p2[15]];*/

		__m128i a = _mm_load_si128((__m128i*)p1);
		__m128i b = _mm_loadu_si128((__m128i*)p2);
		__m128i r = _mm_sad_epu8(a, b);
		s += _mm_extract_epi16(r, 0) + _mm_extract_epi16(r, 4);

		if (s > distlim)	break;

		p1 += lx;
		p2 += lx;
	}
	return s;
}


//---------------------------------------------------------------------
//		�t���[���Ԑ�Βl�����v�֐�(SSE�o�[�W����)
//---------------------------------------------------------------------
int dist_SSE( unsigned char *p1, unsigned char *p2, int lx, int distlim, int block_hight )
{
	int s = 0;
/*
dist_normal������ƕ�����悤�ɁAp1��p2�̐�Βl���𑫂��Ă��Adistlim�𒴂����炻�̍��v��Ԃ������B
block_hight�ɂ�8��16���������Ă���A�O�҂̓t�B�[���h�����A��҂��t���[�������p�B
block_hight��8���������Ă�����΁Alx�ɂ͉摜�̉������������Ă���B
block_hight��16���������Ă�����΁Alx�ɂ͉摜�̉����̓�{�̒l���������Ă���B
�ǂȂ����A�������쐬���Ă�����������΁A���Ɋ��ӂ������܂��B
*/
	return s;
}


//---------------------------------------------------------------------
//		�u���b�N���̍ő�P�x���擾�֐�
//---------------------------------------------------------------------
int maxmin_block( unsigned char *p, int lx, int block_height )
{
	__m128i rmin, rmax, a, b, z;

	// �e��̍ő�E�ŏ������߂�
	rmin = _mm_load_si128((__m128i*)p);
	rmax = _mm_load_si128((__m128i*)p);
	p += lx;
	for(int i=1; i<block_height; i++){
		a = _mm_load_si128((__m128i*)p);
		rmin = _mm_min_epu8(rmin, a);
		rmax = _mm_max_epu8(rmax, a);
		p += lx;
	}
	// ��Ԃ̍ő�E�ŏ������߂�
	// 16�f�[�^�̍ő�E�ŏ����W�f�[�^�ɍi��
	z    = _mm_setzero_si128();
	a    = _mm_unpackhi_epi8(rmin, z);
	b    = _mm_unpacklo_epi8(rmin, z);
	rmin = _mm_min_epi16(a, b);
	a    = _mm_unpackhi_epi8(rmax, z);
	b    = _mm_unpacklo_epi8(rmax, z);
	rmax = _mm_max_epi16(a, b);
	// 8����4
	a    = _mm_unpackhi_epi16(rmin, z);
	b    = _mm_unpacklo_epi16(rmin, z);
	rmin = _mm_min_epi16(a, b);
	a    = _mm_unpackhi_epi16(rmax, z);
	b    = _mm_unpacklo_epi16(rmax, z);
	rmax = _mm_max_epi16(a, b);
	// 4����2
	a    = _mm_unpackhi_epi32(rmin, z);
	b    = _mm_unpacklo_epi32(rmin, z);
	rmin = _mm_min_epi16(a, b);
	a    = _mm_unpackhi_epi32(rmax, z);
	b    = _mm_unpacklo_epi32(rmax, z);
	rmax = _mm_max_epi16(a, b);
	// 2����1
	a    = _mm_unpackhi_epi64(rmin, z);
	b    = _mm_unpacklo_epi64(rmin, z);
	rmin = _mm_min_epi16(a, b);
	a    = _mm_unpackhi_epi64(rmax, z);
	b    = _mm_unpacklo_epi64(rmax, z);
	rmax = _mm_max_epi16(a, b);
	// ���ʎ��o��
	int val_min = _mm_extract_epi16(rmin, 0);
	int val_max = _mm_extract_epi16(rmax, 0);

	return val_max - val_min;
}
