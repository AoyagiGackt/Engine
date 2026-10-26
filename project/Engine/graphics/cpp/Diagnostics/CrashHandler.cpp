/**
 * @file CrashHandler.cpp
 * @brief 処理されなかった例外を受けてログとダンプを書き出す処理（CrashHandler）の実装
 * @note 報告（.txt）には、例外の内容・直前の場面・落ちた場所の呼び出し履歴・直前の出来事（CrashBreadcrumbs）を書く。
 *       アクセス違反などの例外だけでなく、abort（assert の失敗を含む）・純粋仮想関数の呼び出し・不正な引数・std::terminate も拾う
 */
#include "CrashHandler.h"
#include "CrashBreadcrumbs.h"
#include "CrashContext.h"
#include "Logger.h"
#include <DbgHelp.h>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

// 診断文字列の容量
constexpr size_t kMessageCapacity = 128;
constexpr size_t kReportPathCapacity = 256;
constexpr int kMaxStackFrames = 48; // 呼び出し履歴を何段までたどるか
constexpr size_t kSymbolNameCapacity = 256;

/** @brief 落ちた場所の呼び出し履歴を、関数名とファイル名・行番号付きで書き出す */
void WriteStackTrace(std::ofstream& report, const CONTEXT& context) {
#ifdef _M_X64
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    if (!SymInitialize(process, nullptr, TRUE)) {
        report << "（呼び出し履歴を読めませんでした: SymInitialize に失敗）\n";
        return;
    }
    // StackWalk64 は CONTEXT を書き換えるので写しを渡す
    CONTEXT walkContext = context;
    STACKFRAME64 frame = { };
    frame.AddrPC.Offset = walkContext.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = walkContext.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = walkContext.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;

    // 例外の中なので、名前の受け皿はヒープを使わずスタックに置く
    alignas(SYMBOL_INFO) char symbolBuffer[sizeof(SYMBOL_INFO) + kSymbolNameCapacity] = { };
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
    report << "--- 落ちた場所の呼び出し履歴（上が落ちた所） ---\n";
    for (int depth = 0; depth < kMaxStackFrames; ++depth) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &walkContext, nullptr,
                SymFunctionTableAccess64, SymGetModuleBase64, nullptr)
            || frame.AddrPC.Offset == 0) {
            break;
        }
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = static_cast<ULONG>(kSymbolNameCapacity - 1);
        DWORD64 symbolOffset = 0;
        const bool hasSymbol = SymFromAddr(process, frame.AddrPC.Offset, &symbolOffset, symbol) != FALSE;
        IMAGEHLP_LINE64 line = { };
        line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
        DWORD lineOffset = 0;
        const bool hasLine = SymGetLineFromAddr64(process, frame.AddrPC.Offset, &lineOffset, &line) != FALSE;

        char text[kReportPathCapacity + kSymbolNameCapacity];
        std::snprintf(text, sizeof(text), "#%02d %s", depth, hasSymbol ? symbol->Name : "(名前なし)");
        report << text;
        if (hasLine) {
            report << "  (" << line.FileName << ":" << line.LineNumber << ")";
        }
        report << '\n';
    }
    SymCleanup(process);
#else
    (void)report;
    (void)context;
#endif
}

/** @brief 直前の出来事（パンくず）を古い順に書き出す */
void WriteBreadcrumbs(std::ofstream& report) {
    report << "--- 直前の出来事（古い順、最後の" << engine::CrashBreadcrumbs::kCapacity << "件まで） ---\n";
    engine::CrashBreadcrumbs::ForEach([&](const engine::CrashBreadcrumbs::Entry& entry) {
        report << "[frame " << entry.frame << "] " << entry.category << ": " << entry.text << '\n';
    });
    report << "（落ちたのは frame " << engine::CrashBreadcrumbs::Frame() << "）\n";
}

/** @brief 直前の出来事をログにも残す（Release では報告のファイルを作らないため） */
void LogBreadcrumbs() {
    engine::CrashBreadcrumbs::ForEach([](const engine::CrashBreadcrumbs::Entry& entry) {
        char text[kMessageCapacity + engine::CrashBreadcrumbs::kTextSize];
        std::snprintf(text, sizeof(text), "Crash breadcrumb [frame %llu] %s: %s",
            static_cast<unsigned long long>(entry.frame), entry.category, entry.text);
        engine::Logger::LogError(text);
    });
}

/**
 * @brief 落ちた時の報告（ログ・.txt・.dmp）を書き出す
 * @param reason        何が起きたか（例外コードや abort など）
 * @param context       落ちた時のレジスタ（呼び出し履歴をたどるのに使う）
 * @param exceptionInfo 例外の情報（例外でない落ち方では nullptr）
 */
void WriteCrashReport(const char* reason, const CONTEXT& context, EXCEPTION_POINTERS* exceptionInfo) {
    // 同時に複数の落ち方が起きても、報告は最初の1回だけにする（報告中にさらに落ちた場合も含む）
    static volatile LONG sReporting = 0;
    if (InterlockedExchange(&sReporting, 1) != 0) {
        return;
    }
    engine::Logger::LogError(reason);
    // 落ちる直前に何をしていたか（場面名と出来事）もログへ出す
    const std::string crashContext = engine::CrashContext::GetInstance().Get();
    engine::Logger::LogError("Crash context: " + crashContext);
    LogBreadcrumbs();

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

    // 例外の内容・直前の場面・落ちた場所・直前の出来事を文字で残す
    std::ofstream report(reportPath);
    if (report) {
        report << reason << '\n';
        report << "context=" << crashContext << '\n';
        report << "thread_id=" << GetCurrentThreadId() << '\n';
        WriteStackTrace(report, context);
        WriteBreadcrumbs(report);
    }

    // デバッガで開けるミニダンプを書き出す（例外でない落ち方は例外の情報なしで書く）
    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mdei = { };
        mdei.ThreadId = GetCurrentThreadId();
        mdei.ExceptionPointers = exceptionInfo;
        mdei.ClientPointers = FALSE;

        if (MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                MiniDumpNormal, exceptionInfo != nullptr ? &mdei : nullptr, nullptr, nullptr)) {
            engine::Logger::LogError(std::string("Crash dump written: ") + path);
        }
        CloseHandle(file);
    }
    engine::Logger::LogError(std::string("Crash report written: ") + reportPath);
#endif
}

/** @brief 例外でない落ち方の報告を、今いる場所のレジスタで書き出す */
void ReportWithoutException(const char* reason) {
    CONTEXT context = { };
    RtlCaptureContext(&context);
    WriteCrashReport(reason, context, nullptr);
}

/** @brief abort（assert の失敗を含む）を受けた時に報告する */
void OnAbortSignal(int) {
    ReportWithoutException("abort（assert の失敗を含む）で終了しました");
}

/** @brief 純粋仮想関数が呼ばれた時（壊れたオブジェクトや破棄中のオブジェクトの呼び出し）に報告する */
void OnPureCall() {
    ReportWithoutException("純粋仮想関数が呼ばれました（破棄済み・作りかけのオブジェクトを呼んだ可能性）");
    std::abort();
}

/** @brief CRT の関数に不正な引数が渡された時に報告する */
void OnInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t) {
    ReportWithoutException("CRT の関数に不正な引数が渡されました");
    std::abort();
}

/** @brief std::terminate（投げた例外を誰も受けなかった時など）に報告する */
void OnTerminate() {
    ReportWithoutException("std::terminate で終了しました（受け取られなかった C++ の例外など）");
    std::abort();
}

}
#pragma comment(lib, "Dbghelp.lib")

namespace engine {

/** @brief 落ちた時の報告を書くハンドラをまとめて登録する */
void CrashHandler::Install() {
    SetUnhandledExceptionFilter(&CrashHandler::Filter);
    std::signal(SIGABRT, &OnAbortSignal);
    _set_purecall_handler(&OnPureCall);
    _set_invalid_parameter_handler(&OnInvalidParameter);
    std::set_terminate(&OnTerminate);
}

/** @brief クラッシュレポートへ記録する現在の処理名を設定する */
void CrashHandler::SetContext(const std::string& context) {
    // 描画のたびに同じ名前で呼ばれるので、直前の出来事には変わった時だけ残す（記録の数には限りがあるため）
    if (CrashContext::GetInstance().Get() != context) {
        CrashBreadcrumbs::Add("scene", context);
    }
    CrashContext::GetInstance().Set(context);
}

/** @brief 処理されなかった例外を受け取り、ログとクラッシュ情報を書き出す */
LONG WINAPI CrashHandler::Filter(EXCEPTION_POINTERS* exceptionInfo) {
    // 例外コードと発生アドレスを報告の1行目にする
    char message[kMessageCapacity];
    std::snprintf(message, sizeof(message), "Unhandled exception: code=0x%08lX address=%p",
        static_cast<unsigned long>(exceptionInfo->ExceptionRecord->ExceptionCode),
        exceptionInfo->ExceptionRecord->ExceptionAddress);
    WriteCrashReport(message, *exceptionInfo->ContextRecord, exceptionInfo);

    // 例外はここで処理済みとして、プロセスを終了させる
    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace engine
