// Test-only explicit application-return receipt, before CRT/OS detachment. Never included by production targets.
#pragma once
#if defined(_WIN32)
#include "../src/core/platform.h"
#include <windows.h>
namespace console_completion_fixture {
inline constexpr wchar_t variable[]=L"CANARY_CONSOLE_FIXTURE_COMPLETION_V1";
inline void complete()noexcept{
    wchar_t path[32768]{};const DWORD size=GetEnvironmentVariableW(variable,path,32768);if(!size||size>=32768)return;
    const HANDLE file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return;
    const DWORD record[]={0x434d494f,GetCurrentProcessId()};DWORD written=0;
    (void)WriteFile(file,record,sizeof(record),&written,nullptr);CloseHandle(file);
}
inline void disableChild(){SetEnvironmentVariableW(variable,nullptr);}
}
#else
namespace console_completion_fixture {inline void complete(){} inline void disableChild(){}}
#endif
