/**
 * @file CrashHandler.cpp
 * @brief 処理されなかった例外を受けてログとダンプを書き出す処理（CrashHandler）の実装
 */
#include "CrashHandler.h"
#include "CrashContext.h"
#include "Logger.h"
#include <DbgHelp.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

// 診断文字列の容量
constexpr size_t kMessageCapacity = 128;
constexpr size_t kReportPathCapacity = 256;

}
#pragma comment(lib, "Dbghelp.lib")

namespace engine {

/** @brief SetUnhandledExceptionFilterにハンドラを登録する */
void CrashHandler::Install()
{
    SetUnhandledExceptionFilter(&CrashHandler::Filter);
}

/** @brief クラッシュレポートへ記録する現在の処理名を設定する */
void CrashHandler::SetContext(const std::string& context)
{
    CrashContext::GetInstance().Set(context);
}

/** @brief 処理されなかった例外を受け取り、ログとクラッシュ情報を書き出す */
LONG WINAPI CrashHandler::Filter(EXCEPTION_POINTERS* exceptionInfo)
{
    // 例外コードと発生アドレスをログへ出す
    char message[kMessageCapacity];
    std::snprintf(message, sizeof(message), "Unhandled exception: code=0x%08lX address=%p",
        static_cast<unsigned long>(exceptionInfo->ExceptionRecord->ExceptionCode),
        exceptionInfo->ExceptionRecord->ExceptionAddress);
    Logger::LogError(message);

    // 落ちる直前に何をしていたか（場面名など）もログへ出す
    const std::string context = CrashContext::GetInstance().Get();
    Logger::LogError("Crash context: " + context);

#ifndef ENGINE_RELEASE
    // 開発中だけ crash フォルダへダンプと記録を書き出す
    std::error_code ec;
    std::filesystem::create_directories("crash", ec);

    // ファイル名は発生日時にし、ダンプ(.dmp)と状況の記録(.txt)で同じ名前を使う
    SYSTEMTIME st;
    GetLocalTime(&st);
    char baseName[kReportPathCapacity];
    std::snprintf(baseName, sizeof(baseName), "crash/crash_%04d%02d%02d_%02d%02d%02d",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    // 例外の中で呼ばれるので、ヒープを使わずスタックの文字配列で組み立てる
    char path[kReportPathCapacity];
    std::snprintf(path, sizeof(path), "%s.dmp", baseName);
    char reportPath[kReportPathCapacity];
    std::snprintf(reportPath, sizeof(reportPath), "%s.txt", baseName);

    // 例外の内容・直前の場面・スレッドを文字で残す
    std::ofstream report(reportPath);
    if (report)
    {
        report << message << '\n';
        report << "context=" << context << '\n';
        report << "thread_id=" << GetCurrentThreadId() << '\n';
    }

    // デバッガで開けるミニダンプを書き出す
    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE)
    {
        MINIDUMP_EXCEPTION_INFORMATION mdei = { };
        mdei.ThreadId = GetCurrentThreadId();
        mdei.ExceptionPointers = exceptionInfo;
        mdei.ClientPointers = FALSE;

        if (MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                MiniDumpNormal, &mdei, nullptr, nullptr))
        {
            Logger::LogError(std::string("Crash dump written: ") + path);
        }
        CloseHandle(file);
    }
#endif

    // 例外はここで処理済みとして、プロセスを終了させる
    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace engine
