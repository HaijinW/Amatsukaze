/**
* Amtasukaze CLI Entry point
* Copyright (c) 2017 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#include "AmatsukazeCLI.hpp"

#ifdef _MSC_VER
#include <shellapi.h>
// �R���\�[���Ȃ��ŗ����グ��ɂ�mainCRTStartup�͎g���Ȃ��̂�WinMainCRTStartup���g��
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	int argc;
	LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (argv == NULL) {
		printf("CommandLineToArgvW failed\n");
		return 1;
	}
	try {
		printCopyright();
		return amatsukazeTranscodeMain(parseArgs(argc, argv));
	}
	catch (Exception e) {
		// parseArgs�ŃG���[
		printHelp(argv[0]);
		return 1;
	}
	// �{���͎g���I�������argv�͉�����Ȃ��Ƃ����Ȃ�
	// LocalFree(argv);
}
#else
int main(int argc, char* argv[]) {
	try {
		printCopyright();
		return amatsukazeTranscodeMain(parseArgs(argc, argv));
	}
	catch (Exception e) {
		// parseArgs�ŃG���[
		printHelp(argv[0]);
		return 1;
	}
}
#endif
