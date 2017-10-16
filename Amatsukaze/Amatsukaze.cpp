/**
* Amtasukaze Compile Target
* Copyright (c) 2017 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#define _USE_MATH_DEFINES
// avisynth�Ƀ����N���Ă���̂�
#define AVS_LINKAGE_DLLIMPORT
#include "AmatsukazeCLI.hpp"

// Avisynth�t�B���^�f�o�b�O�p
#include "TextOut.cpp"

HMODULE g_DllHandle;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved) {
	if (dwReason == DLL_PROCESS_ATTACH) g_DllHandle = hModule;
	return TRUE;
}

// CM��͗p�i�{�f�o�b�O�p�j�C���^�[�t�F�[�X
extern "C" __declspec(dllexport) const char* __stdcall AvisynthPluginInit3(IScriptEnvironment* env, const AVS_Linkage* const vectors) {
	// ���ڃ����N���Ă���̂�vectors���i�[����K�v�͂Ȃ�

	// FFMPEG���C�u����������
	av_register_all();

	env->AddFunction("AMTSource", "s", av::CreateAMTSource, 0);
	env->AddFunction("AMTEraseLogo", "cs[mode]i[maskratio]i", logo::AMTEraseLogo::Create, 0);

	return "Amatsukaze plugin";
}
