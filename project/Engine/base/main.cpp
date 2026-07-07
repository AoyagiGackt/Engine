#include <Windows.h>
#include "CrashHandler.h"
#include "Game.h"
#include "D3DResourceLeakChecker.h"
#include <memory>
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

// --------------------------------------------------
// メイン関数
// --------------------------------------------------

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    // 未処理例外が起きてもダンプとログを残せるよう最初に登録する
    CrashHandler::Install();

    // リークチェッカーを最初に宣言
    D3D12ResourceLeakChecker leakCheck;

    // ゲームインスタンス生成
    std::unique_ptr<Framework> game = std::make_unique<MyGame>();

    // ゲーム実行
    game->Run();

    return 0;
}