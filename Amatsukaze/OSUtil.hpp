/**
* Amtasukaze OS Utility
* Copyright (c) 2017-2018 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include <Windows.h>
#include "Shlwapi.h"

#include <string>

extern HMODULE g_DllHandle;

std::string GetModulePath() {
	char buf[MAX_PATH] = { 0 };
	GetModuleFileName(g_DllHandle, buf, MAX_PATH);
	return buf;
}

std::string GetModuleDirectory() {
	char buf[MAX_PATH] = { 0 };
	GetModuleFileName(g_DllHandle, buf, MAX_PATH);
	PathRemoveFileSpecA(buf);
	return buf;
}

std::string SearchExe(const std::string& name) {
	char buf[MAX_PATH] = { 0 };
	if (!SearchPath(0, name.c_str(), 0, MAX_PATH, buf, 0)) {
		return name;
	}
	return buf;
}

std::string GetDirectoryPath(const std::string& name) {
	char buf[MAX_PATH] = { 0 };
	std::copy(name.begin(), name.end(), buf);
	PathRemoveFileSpecA(buf);
	return buf;
}

bool DirectoryExists(const std::string& dirName_in)
{
  DWORD ftyp = GetFileAttributesA(dirName_in.c_str());
  if (ftyp == INVALID_FILE_ATTRIBUTES)
    return false;
  if (ftyp & FILE_ATTRIBUTE_DIRECTORY)
    return true;
  return false;
}

// dirpath�� �I�[\\�Ȃ�
// pattern�� "*.*" �Ƃ�
// �f�B���N�g�������܂܂Ȃ��t�@�C�������X�g���Ԃ�
std::vector<std::string> GetDirectoryFiles(const std::string& dirpath, const std::string& pattern)
{
	std::string search = dirpath + "\\" + pattern;
	std::vector<std::string> result;
	WIN32_FIND_DATA findData;
	HANDLE hFind = FindFirstFile(search.c_str(), &findData);
	if (hFind == INVALID_HANDLE_VALUE) {
		THROWF(IOException, "�t�@�C���񋓂Ɏ��s: %s", search);
	}
	do {
		if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			// �f�B���N�g��
		}
		else {
			// �t�@�C��
			result.push_back(findData.cFileName);
		}
	} while (FindNextFile(hFind, &findData));
	FindClose(hFind);
	return result;
}

// ���݂̃X���b�h�ɐݒ肳��Ă���R�A�����擾
int GetProcessorCount()
{
	GROUP_AFFINITY gaffinity;
	if (GetThreadGroupAffinity(GetCurrentThread(), &gaffinity)) {
		int cnt = 0;
		for (int i = 0; i < 64; ++i) {
			if (gaffinity.Mask & (DWORD_PTR(1) << i)) cnt++;
		}
		return cnt;
	}
	return 8; // ���s������K���Ȓl�ɂ��Ă���
}
