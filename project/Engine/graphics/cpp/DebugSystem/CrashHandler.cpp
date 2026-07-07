#include "CrashHandler.h"
#include "Logger.h"
#include <DbgHelp.h>
#include <cstdio>
#include <filesystem>
#pragma comment(lib, "Dbghelp.lib")

namespace engine {

void CrashHandler::Install()
{
    SetUnhandledExceptionFilter(&CrashHandler::Filter);
}

LONG WINAPI CrashHandler::Filter(EXCEPTION_POINTERS* exceptionInfo)
{
    char message[128];
    std::snprintf(message, sizeof(message), "Unhandled exception: code=0x%08lX address=%p",
        static_cast<unsigned long>(exceptionInfo->ExceptionRecord->ExceptionCode),
        exceptionInfo->ExceptionRecord->ExceptionAddress);
    Logger::LogError(message);

    std::error_code ec;
    std::filesystem::create_directories("crash", ec);

    SYSTEMTIME st;
    GetLocalTime(&st);
    char path[256];
    std::snprintf(path, sizeof(path), "crash/crash_%04d%02d%02d_%02d%02d%02d.dmp",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mdei = {};
        mdei.ThreadId          = GetCurrentThreadId();
        mdei.ExceptionPointers = exceptionInfo;
        mdei.ClientPointers    = FALSE;

        if (MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
            MiniDumpNormal, &mdei, nullptr, nullptr)) {
            Logger::LogError(std::string("Crash dump written: ") + path);
        }
        CloseHandle(file);
    }

    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace engine
