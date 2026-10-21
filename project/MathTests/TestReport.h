/**
 * @file TestReport.h
 * @brief テストの出力（VSのエラー一覧・出力ウィンドウとGitHub Actionsのログで読める形）をまとめたファイル
 */
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cmath>
#include <string>
#include <vector>
#include <cstdarg>
#include <cstdio>
#include <source_location>

namespace engine::tests::TestReport {

/// 失敗行に付けるエラーコード（ゲーム本体のビルド後処理のエラーと見分けるため専用にする）
inline constexpr const char* kErrorCode = "MATHTEST";
/// 1回の出力で扱う最大文字数
inline constexpr int kMaxMessageLength = 1024;
/// 変換先の文字コードで1文字が取りうる最大バイト数
inline constexpr int kMaxBytesPerChar = 4;

/**
 * @brief printf と同じ書式で、出力先が読める文字コードに直して出す
 * @note ソースは UTF-8 だが、ビルド後イベントの出力ウィンドウはコンソールの文字コード（日本語環境では Shift-JIS）
 *       として読むので、そのまま出すと日本語が化ける。コンソールへ直接出す時は WriteConsoleW で、
 *       パイプ（ビルド後イベント）へ出す時はコンソールの文字コードへ変換して出す
 */
inline void Print(const char* format, ...)
{
    char utf8[kMaxMessageLength];
    va_list args;
    va_start(args, format);
    std::vsnprintf(utf8, sizeof(utf8), format, args);
    va_end(args);

    wchar_t wide[kMaxMessageLength];
    const int wideLength = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, kMaxMessageLength);
    if (wideLength <= 0)
    {
        std::fputs(utf8, stdout);
        return;
    }

    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD consoleMode = 0;
    if (GetConsoleMode(output, &consoleMode))
    {
        DWORD written = 0;
        WriteConsoleW(output, wide, static_cast<DWORD>(wideLength - 1), &written, nullptr);
        return;
    }

    UINT codePage = GetConsoleOutputCP();
    if (codePage == 0)
    {
        codePage = GetOEMCP();
    }
    char local[kMaxMessageLength * kMaxBytesPerChar];
    if (WideCharToMultiByte(codePage, 0, wide, -1, local, sizeof(local), nullptr, nullptr) > 0)
    {
        std::fputs(local, stdout);
    }
    else
    {
        std::fputs(utf8, stdout);
    }
    std::fflush(stdout);
}

/**
 * @brief テストの失敗を ファイル(行): error MATHTEST: 日本語の説明 の形で出す
 * @param name   テスト名
 * @param detail 正しい値と実際の値など（無ければ nullptr）
 * @param where  失敗したチェックを書いた場所（呼び出し元で自動的に入る）
 * @note この形の行はMSBuildがエラーとして拾うので、ビルド後のテスト実行で失敗すると
 *       VSのエラー一覧に並び、ダブルクリックでそのチェックの行へ飛べる
 */
inline void Failure(const char* name, const char* detail, const std::source_location& where)
{
    const unsigned line = static_cast<unsigned>(where.line());
    if (detail != nullptr)
    {
        Print("%s(%u): error %s: 「%s」のテストが失敗。%s\n", where.file_name(), line, kErrorCode, name, detail);
    }
    else
    {
        Print("%s(%u): error %s: 「%s」のテストが失敗。期待した結果になっていません\n", where.file_name(), line, kErrorCode, name);
    }
}


/** @brief テストの結果を持ち、比較・集計・表示をまとめて行う */
class TestResults {
public:
    static constexpr float kDefaultEpsilon = 1e-4f;

    /** @brief 条件を記録し、失敗したら呼び出し元の位置と一緒に表示する */
    void Check(bool condition, const std::string& name,
        const std::source_location& where = std::source_location::current())
    {
        Record(condition, name, nullptr, where);
    }

    /** @brief 数値を誤差の範囲で比べ、失敗したら期待値と実際の値を表示する */
    void CheckNear(float actual, float expected, const std::string& name,
        float epsilon = kDefaultEpsilon,
        const std::source_location& where = std::source_location::current())
    {
        char detail[kMaxMessageLength];
        std::snprintf(detail, sizeof(detail), "Expected %.4f, actual %.4f (difference %.4f)",
            expected, actual, std::fabs(actual - expected));
        Record(std::fabs(actual - expected) <= epsilon, name, detail, where);
    }

    /** @brief 登録したテストの区切りを実行し、成功した数を表示する */
    void RunSection(const char* name, void (*run)(TestResults&))
    {
        Print("\n-- %s --\n", name);
        const int beforeTotal = testCount_;
        const int beforeFail = FailureCount();
        run(*this);
        const int total = testCount_ - beforeTotal;
        const int failures = FailureCount() - beforeFail;
        Print("   %d / %d passed\n", total - failures, total);
    }

    /** @brief 失敗したテストの名前と合計を表示する */
    void PrintSummary(const char* name) const
    {
        if (!failedNames_.empty())
        {
            Print("\n[Failed checks]\n");
            for (const auto& failed : failedNames_)
            {
                Print("  - %s\n", failed.c_str());
            }
        }
        Print("\n%s total: %d / %d passed\n", name, testCount_ - FailureCount(), testCount_);
    }

    /** @brief 失敗した数を返す */
    int FailureCount() const {
        return static_cast<int>(failedNames_.size());
    }

private:
    /** @brief 結果を集計し、失敗を表示する */
    void Record(bool condition, const std::string& name, const char* detail,
        const std::source_location& where)
    {
        ++testCount_;
        if (!condition)
        {
            failedNames_.push_back(name);
            Failure(name.c_str(), detail, where);
        }
    }

    int testCount_ = 0;
    std::vector<std::string> failedNames_;
};

} // namespace engine::tests::TestReport
