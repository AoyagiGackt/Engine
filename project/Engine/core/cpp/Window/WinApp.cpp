/**
 * @file WinApp.cpp
 * @brief ウィンドウ生成・メッセージ処理・フルスクリーン切替（WinApp）の実装
 */
#include "WinApp.h"
#include "ImGuiManager.h"
using namespace engine;
using namespace engine::graphics;

#pragma comment(lib, "winmm.lib")

#ifdef USE_IMGUI

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

#endif

namespace engine {
namespace {
constexpr UINT kTimerResolutionMs = 1; // Sleep などの待ち時間の精度（ミリ秒）

/** @brief 画面上のウィンドウの位置と大きさ */
struct WindowPlacement {
    int x;
    int y;
    int width;
    int height;
};

/** @brief クライアント領域が kClientWidth×kClientHeight になる通常ウィンドウを、画面中央に置く位置と大きさを返す */
WindowPlacement CenteredWindowPlacement() {
    RECT wr = { 0, 0, WinApp::kClientWidth, WinApp::kClientHeight };
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
    const int width = wr.right - wr.left;
    const int height = wr.bottom - wr.top;
    const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    const int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    return { (screenWidth - width) / 2, (screenHeight - height) / 2, width, height };
}
} // namespace

/** @brief ウィンドウプロシージャ（静的メンバ関数） */
LRESULT CALLBACK WinApp::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {

#ifdef USE_IMGUI

    if (ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam)) {
        return true;
    }

#endif

    // メッセージに応じてゲーム固有の処理を行う
    switch (uMsg) {
        // ウィンドウが破棄された
    case WM_DESTROY:
        // OSに対して、アプリの終了を伝える
        PostQuitMessage(0);
        return 0;

    case WM_SIZE: {
        // 最小化中（クライアント領域が0x0になる）はリサイズ処理をスキップする
        if (wParam == SIZE_MINIMIZED) {
            break;
        }

        auto* self = reinterpret_cast<WinApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self && self->resizeCallback_) {
            const int32_t width = static_cast<int32_t>(LOWORD(lParam));
            const int32_t height = static_cast<int32_t>(HIWORD(lParam));
            self->resizeCallback_(width, height);
        }
        break;
    }
    }

    // 標準のメッセージ処理を行う
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

/** @brief ウィンドウシステムの初期化 */
void WinApp::Initialize() {
    // DPI スケーリングを無効化して実ピクセルで描画する
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    HRESULT hr = CoInitializeEx(0, COINIT_MULTITHREADED);

    // WNDCLASSWを使用
    WNDCLASSW wc = { };

    // ウィンドクラスプロシージャ
    wc.lpfnWndProc = WindowProc;

    // ウィンドクラス名
    wc.lpszClassName = L"WindowClass";

    // インスタンスハンドル
    wc.hInstance = GetModuleHandleW(nullptr);

    // カーソル
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);

    // ウィンドクラスの登録
    RegisterClassW(&wc);

    // 後でインスタンスハンドルを返せるよう、登録した設定をメンバに写しておく
    wndClass_ = wc;

    // クライアント領域が kClientWidth×kClientHeight になる大きさで、画面中央に出す
    const WindowPlacement placement = CenteredWindowPlacement();

    hwnd_ = CreateWindowW(
        wc.lpszClassName, // 利用するクラス名
        L"LE3C_01_アオヤギ_ガクト_コンボマン", // タイトルバーの文字
        WS_OVERLAPPEDWINDOW, // 通常ウィンドウ
        placement.x, // 表示x座標（画面中央）
        placement.y, // 表示y座標（画面中央）
        placement.width, // ウィンドウ横幅
        placement.height, // ウィンドウ縦幅
        nullptr, // 親ウィンドウハンドル
        nullptr, // メニューハンドル
        wc.hInstance, // インスタンスハンドル
        nullptr // オプション
    );

    // WindowProc（static関数）から自分自身を参照できるようにしておく（WM_SIZE等で使用）
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    // ウィンドウを表示する
    ShowWindow(hwnd_, SW_NORMAL);

    // システムタイマーの分解能を上げる
    timeBeginPeriod(kTimerResolutionMs);
}

/** @brief ウィンドウの更新処理 */
void WinApp::Update() {
}

/** @brief ウィンドウシステムの終了処理 */
void WinApp::Finalize() {
    timeEndPeriod(kTimerResolutionMs);
    CloseWindow(hwnd_);
    CoUninitialize();
}

/** @brief フルスクリーンとウィンドウモードを切り替える */
void WinApp::ToggleFullscreen() {
    isFullscreen_ = !isFullscreen_;

    if (isFullscreen_) {
        // ボーダーレスフルスクリーンへ
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        SetWindowLongW(hwnd_, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(hwnd_, HWND_TOP, 0, 0, screenWidth, screenHeight, SWP_FRAMECHANGED | SWP_NOACTIVATE);
        ShowWindow(hwnd_, SW_SHOW);
    } else {
        // ウィンドウモードへ（起動時と同じ大きさで画面中央）
        const WindowPlacement placement = CenteredWindowPlacement();
        SetWindowLongW(hwnd_, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPos(hwnd_, HWND_TOP, placement.x, placement.y, placement.width, placement.height, SWP_FRAMECHANGED | SWP_NOACTIVATE);
        ShowWindow(hwnd_, SW_NORMAL);
    }
}

/** @brief Windowsメッセージを処理する */
bool WinApp::ProcessMessage() {
    MSG msg { };

    if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (msg.message == WM_QUIT) {
        return true;
    }

    return false;
}
} // namespace engine
