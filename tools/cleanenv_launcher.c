#define UNICODE
#define _UNICODE
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

/* Start a program with a deliberately case-unique environment.  The desktop
 * host contributes both Path and PATH; MSBuild cannot spawn cl.exe with that
 * pair present. */
int wmain(int argc, wchar_t **argv) {
    wchar_t command[32768] = L"";
    wchar_t environment[] =
        L"ComSpec=C:\\Windows\\System32\\cmd.exe\0"
        L"SystemRoot=C:\\Windows\0"
        L"SystemDrive=C:\\0"
        L"TEMP=C:\\Users\\lucas\\AppData\\Local\\Temp\0"
        L"TMP=C:\\Users\\lucas\\AppData\\Local\\Temp\0"
        L"USERPROFILE=C:\\Users\\lucas\0"
        L"HOMEDRIVE=C:\\0"
        L"HOMEPATH=\\Users\\lucas\0"
        L"APPDATA=C:\\Users\\lucas\\AppData\\Roaming\0"
        L"LOCALAPPDATA=C:\\Users\\lucas\\AppData\\Local\0"
        L"ProgramData=C:\\ProgramData\0"
        L"ProgramFiles=C:\\Program Files\0"
        L"ProgramFiles(x86)=C:\\Program Files (x86)\0"
        L"CommonProgramFiles=C:\\Program Files\\Common Files\0"
        L"CommonProgramFiles(x86)=C:\\Program Files (x86)\\Common Files\0"
        L"USERNAME=lucas\0"
        L"NUMBER_OF_PROCESSORS=8\0"
        L"PROCESSOR_ARCHITECTURE=x86\0"
        L"PATHEXT=.COM;.EXE;.BAT;.CMD\0"
        L"PATH=C:\\Windows\\System32;C:\\Windows;C:\\Program Files\\Git\\cmd\0\0";
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };
    DWORD exit_code = 1;
    int i;
    if (argc < 2) return 2;
    for (i = 1; i < argc; ++i) {
        if (i > 1) wcscat_s(command, _countof(command), L" ");
        wcscat_s(command, _countof(command), L"\"");
        wcscat_s(command, _countof(command), argv[i]);
        wcscat_s(command, _countof(command), L"\"");
    }
    if (!CreateProcessW(NULL, command, NULL, NULL, FALSE,
            CREATE_UNICODE_ENVIRONMENT, environment, NULL, &si, &pi)) {
        fwprintf(stderr, L"CreateProcess failed: %lu\n", GetLastError());
        return 3;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)exit_code;
}
