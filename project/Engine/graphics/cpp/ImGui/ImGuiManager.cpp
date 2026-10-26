/**
 * @file ImGuiManager.cpp
 * @brief ImGui の初期化・フレームの開始と終了・描画（ImGuiManager）の実装
 */
#include "ImGuiManager.h"
#include <SrvManager.h>

namespace engine::graphics {
namespace {

// 日本語フォントの表示サイズ
constexpr float kJapaneseFontSize = 16.0f;

}

/** @brief ImGuiの初期化処理コンテキストの作成やDX12・Win32への紐づけを行う */
void ImGuiManager::Initialize([[maybe_unused]] WinApp* winApp, [[maybe_unused]] DirectXCommon* dxCommon) {
#ifdef USE_IMGUI

    // ImGui生成
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    // ウィンドウはタイトルバーをつかんだ時だけ動かす（中身のクリックやドラッグでずれないように）
    ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;

    // 日本語フォント読み込み（Windows 標準の游ゴシック）
    // ※ Win32Init より前に登録しないとアトラスに反映されない
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->AddFontFromFileTTF(
        "C:/Windows/Fonts/YuGothM.ttc",
        kJapaneseFontSize,
        nullptr,
        io.Fonts->GetGlyphRangesJapanese());

    // Win32初期化
    ImGui_ImplWin32_Init(winApp->GetHwnd());

    // DirectX12初期化
    uint32_t index = SrvManager::GetInstance()->Allocate();

    ImGui_ImplDX12_Init(
        dxCommon->GetDevice(),
        dxCommon->GetBufferCount(),
        dxCommon->GetBackBufferFormat(),
        SrvManager::GetInstance()->GetSrvDescriptorHeap(), // ヒープを渡す
        SrvManager::GetInstance()->GetCPUDescriptorHandle(index), // 確保したCPUハンドル
        SrvManager::GetInstance()->GetGPUDescriptorHandle(index) // 確保したGPUハンドル
    );

    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

#endif
}

/** @brief ImGuiの新しいフレーム開始処理 */
void ImGuiManager::Begin() {
#ifdef USE_IMGUI

    // フレーム開始
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

#endif
}

/** @brief ImGuiのフレーム終了（UI構築完了）処理 */
void ImGuiManager::End() {
#ifdef USE_IMGUI

    // 内部コマンド生成
    ImGui::Render();

#endif
}

/** @brief ImGuiの実際の描画コマンドを積む */
void ImGuiManager::Draw(DirectXCommon* dxCommon) {
#ifdef USE_IMGUI

    // コマンドリストをdxCommonから取得する
    ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();

    ID3D12DescriptorHeap* ppHeaps[] = { SrvManager::GetInstance()->GetSrvDescriptorHeap() };
    commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

    // 描画コマンドを発行
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);

#endif
}

/** @brief ImGuiの終了処理とリソース解放 */
void ImGuiManager::Finalize() {
#ifdef USE_IMGUI

    // 終了処理
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

#endif
}
} // namespace engine::graphics
