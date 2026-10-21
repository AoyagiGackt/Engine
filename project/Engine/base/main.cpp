/**
 * @file main.cpp
 * @brief アプリケーションの入口。クラッシュ時の処理を登録し、ゲームを起動して終了まで回す
 */
#include "CrashHandler.h"
#include "D3DResourceLeakChecker.h"
#include "Game.h"
#include "Logger.h"
#include <Windows.h>
#include <memory>
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

// メイン関数

/** @brief 例外処理と診断を初期化してゲームのメインループを実行する */
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    // 未処理例外が起きてもダンプとログを残せるよう最初に登録する
    CrashHandler::Install();

    // どのビルドで動かしたログかを後から追跡できるよう先頭に1行残す
    Logger::LogInfo(std::string("Engine build: ") + __DATE__ + " " + __TIME__
#ifdef _DEBUG
        + " [Development]"
#elif defined(ENGINE_RELEASE)
        + " [Release]"
#elif defined(ENGINE_DEVELOPMENT)
        + " [Development]"
#else
        + " [Unknown]"
#endif
    );

    // リークチェッカーを最初に宣言
    D3D12ResourceLeakChecker leakCheck;

    // ゲームインスタンス生成
    std::unique_ptr<Framework> game = std::make_unique<MyGame>();

    // ゲーム実行
    game->Run();

    return 0;
}
