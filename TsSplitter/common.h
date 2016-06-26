#pragma once

// �^�[�Q�b�g�� Windows Vista �ɐݒ�
#define _WIN32_WINNT 0x0600 // _WIN32_WINNT_VISTA
#include <SDKDDKVer.h>

#define WIN32_LEAN_AND_MEAN             // Windows �w�b�_�[����g�p����Ă��Ȃ����������O���܂��B
// Windows �w�b�_�[ �t�@�C��:
#include <windows.h>

// min, max�}�N���͕K�v�Ȃ��̂ō폜
#undef min
#undef max

// TODO: �v���O�����ɕK�v�Ȓǉ��w�b�_�[�������ŎQ�Ƃ��Ă�������
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>

#include <algorithm>

inline void assertion_failed(const char* line, const char* file, int lineNum) {
	char buf[500];
	sprintf_s(buf, "Assertion failed!! %s (%s:%d)", line, file, lineNum);
	printf("%s\n", buf);
	throw buf;
}

#ifndef _DEBUG
#define ASSERT(exp)
#else
#define ASSERT(exp) do { if(!(exp)) assertion_failed(#exp, __FILE__, __LINE__); } while(0)
#endif

inline int __builtin_clzl(uint64_t mask) {
	DWORD index;
#ifdef _WIN64
	_BitScanReverse64(&index, mask);
#else
	DWORD highWord = (DWORD)(mask >> 32);
	DWORD lowWord = (DWORD)mask;
	if (highWord) {
		_BitScanReverse(&index, highWord);
		index += 32;
	}
	else {
		_BitScanReverse(&index, lowWord);
	}
#endif
	return index;
}
