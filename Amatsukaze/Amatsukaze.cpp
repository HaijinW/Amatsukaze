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
#include "LogoGUISupport.hpp"

// Avisynth�t�B���^�f�o�b�O�p
#include "TextOut.cpp"

HMODULE g_DllHandle;
bool g_av_initialized = false;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved) {
	if (dwReason == DLL_PROCESS_ATTACH) g_DllHandle = hModule;
	return TRUE;
}

extern "C" __declspec(dllexport) void InitAmatsukazeDLL()
{
	// FFMPEG���C�u����������
	av_register_all();
}

static void init_console()
{
	AllocConsole();
	freopen("CONOUT$", "w", stdout);
	freopen("CONIN$", "r", stdin);
}

// CM��͗p�i�{�f�o�b�O�p�j�C���^�[�t�F�[�X
extern "C" __declspec(dllexport) const char* __stdcall AvisynthPluginInit3(IScriptEnvironment* env, const AVS_Linkage* const vectors) {
	// ���ڃ����N���Ă���̂�vectors���i�[����K�v�͂Ȃ�

	if (g_av_initialized == false) {
		// FFMPEG���C�u����������
		av_register_all();
		g_av_initialized = true;
	}

	env->AddFunction("AMTSource", "s", av::CreateAMTSource, 0);
	env->AddFunction("AMTEraseLogo", "cs[mode]i[maskratio]i", logo::AMTEraseLogo::Create, 0);

	env->AddFunction("AMTAnalyzeLogo", "cs[maskratio]i", logo::AMTAnalyzeLogo::Create, 0);
	env->AddFunction("AMTEraseLogo2", "ccs[mode]i", logo::AMTEraseLogo2::Create, 0);

	return "Amatsukaze plugin";
}
