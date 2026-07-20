/**
 * @file CrashHandler.h
 * @brief 未処理例外（クラッシュ）発生時にミニダンプとログを残すハンドラを定義するファイル
 */
#pragma once
#include <Windows.h>
#include <string>
namespace engine {

/**
 * @brief 未処理例外発生時にミニダンプファイル(.dmp)を出力し、ログにも記録するクラス
 * @note Install() をアプリ起動直後（他の初期化より前）に1回呼ぶだけで有効になる
 */
class CrashHandler {
public:
    /** @brief SetUnhandledExceptionFilterにハンドラを登録する */
    static void Install();

    /**
     * @brief クラッシュレポートへ記録する現在の処理名を設定する
     * @param context シーン名やロード処理名
     */
    static void SetContext(const std::string& context);

private:
    static LONG WINAPI Filter(EXCEPTION_POINTERS* exceptionInfo);
};

} // namespace engine
