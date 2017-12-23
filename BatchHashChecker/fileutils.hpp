#pragma once

#include "common.h"

// EnumDirectoryFile�ɓn���֐��i�I�u�W�F�N�gor�|�C���^�j�̃^�C�v
// �߂�l:�񋓂𑱍s����ꍇtrue�A�I������ꍇfalse
typedef bool(*EnumDirectoryFileFunc)(WIN32_FIND_DATA& FindData);

namespace impl {

template <class cbType> bool inline _CallFileFuncW(WIN32_FIND_DATAW& data, cbType& cbFunc)
{
	DWORD dwHead = *(DWORD*)data.cFileName;
	unsigned __int64 qwHead = *(unsigned __int64*)data.cFileName;
	// "."�łȂ�����".."�łȂ�
	if ((dwHead ^ 0x0000002E) && ((qwHead ^ 0x002E002E) & 0xFFFFFFFFFFFFLL)) {
		//if( wcscmp(data.cFileName, L".") && wcscmp(data.cFileName, L"..") ){
		return cbFunc(data);
	}
	return true;
}
template <class cbType> bool inline _CallFileFuncA(WIN32_FIND_DATAA& data, cbType& cbFunc)
{
	WORD wHead = *(WORD*)data.cFileName;
	DWORD dwHead = *(DWORD*)data.cFileName;
	if ((wHead ^ 0x2E) && ((dwHead ^ 0x2E2E) & 0xFFFFFF)) {
		return cbFunc(data);
	}
	return true;
}

}
// bEasyPath�̓p�X�̎w����@
// true�̏ꍇ
// '\\'�͂����Ă��Ȃ��Ă��悢
// '*'��'?'�͎g���Ȃ�
// API��������A���[�g�t�H���_��l�b�g���[�N�t�H���_��
// �w�肷��Ƃ���'\\'��t���Ȃ���΂Ȃ�Ȃ�
// false�̏ꍇ��API����
template <class cbType> bool EnumDirectoryFile(const wchar_t* pszPath, cbType& cbFunc, bool bEasyPath = true)
{
	wchar_t nameBuf[MAX_PATH];
	if (bEasyPath) {
		size_t len = wcslen(pszPath);
		if (pszPath[len - 1] == L'\\') {
			if (len >= MAX_PATH - 1) {
				return false;
			}
			memcpy(nameBuf, pszPath, len * sizeof(wchar_t));
			nameBuf[len] = L'*';
			nameBuf[len + 1] = L'\0';
			pszPath = nameBuf;
		}
	}
	WIN32_FIND_DATAW FindData;
	HANDLE hFind = FindFirstFileW(pszPath, &FindData);
	if (hFind == INVALID_HANDLE_VALUE) {
		return (GetLastError() == ERROR_NO_MORE_FILES);
	}
	else {
		bool bContinue = impl::_CallFileFuncW(FindData, cbFunc);
		while (bContinue) {
			if (!FindNextFileW(hFind, &FindData)) {
				bool bRet = (GetLastError() == ERROR_NO_MORE_FILES);
				FindClose(hFind);
				return bRet;
			}
			bContinue = impl::_CallFileFuncW(FindData, cbFunc);
		}
		FindClose(hFind);
	}
	return true;
}

template <class cbType> bool EnumDirectoryFile(LPCSTR pszPath, cbType& cbFunc, bool bEasyPath = true)
{
	char nameBuf[MAX_PATH];
	if (bEasyPath) {
		size_t len = strlen(pszPath);
		if (pszPath[len - 1] == '\\') {
			if (len >= MAX_PATH - 1) {
				return false;
			}
			memcpy(nameBuf, pszPath, len * sizeof(wchar_t));
			nameBuf[len] = '*';
			nameBuf[len + 1] = '\0';
			pszPath = nameBuf;
		}
	}
	WIN32_FIND_DATAA FindData;
	HANDLE hFind = FindFirstFileA(pszPath, &FindData);
	if (hFind == INVALID_HANDLE_VALUE) {
		return (GetLastError() == ERROR_NO_MORE_FILES);
	}
	else {
		bool bContinue = impl::_CallFileFuncA(FindData, cbFunc);
		while (bContinue) {
			if (!FindNextFileA(hFind, &FindData)) {
				bool bRet = (GetLastError() == ERROR_NO_MORE_FILES);
				FindClose(hFind);
				return bRet;
			}
			bContinue = impl::_CallFileFuncA(FindData, cbFunc);
		}
		FindClose(hFind);
	}
	return true;
}
