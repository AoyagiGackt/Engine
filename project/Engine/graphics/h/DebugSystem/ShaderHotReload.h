#pragma once
#include "DirectXCommon.h"
#include <d3d12.h>
#include <dxcapi.h>
#include <wrl/client.h>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
namespace engine::graphics {

// シェーダーファイルの変更を監視し、PSO を自動で再生成するクラス
//
// 使い方:
//   auto* hr = ShaderHotReload::GetInstance();
//   hr->Initialize(dxCommon);
//
//   uint32_t id = hr->Register(
//       L"Resources/shaders/foo/FooPS.hlsl", L"ps_6_0",
//       [&](IDxcBlob* newPS) { /* PSO を作り直す */ });
//
//   // 毎フレーム
//   hr->Update();   // タイムスタンプを確認し、変更があれば再コンパイル＆コールバック
class ShaderHotReload {
public:
    static ShaderHotReload* GetInstance() {
        static ShaderHotReload inst;
        return &inst;
    }

    void Initialize(engine::DirectXCommon* dxCommon);

    // シェーダーを登録。変更検知時に onRecompiled(newBlob) を呼ぶ
    // 戻り値: 登録 ID（Unregister に使用）
    uint32_t Register(const std::wstring& path, const wchar_t* profile,
                      std::function<void(IDxcBlob*)> onRecompiled);

    void Unregister(uint32_t id);

    // ファイル更新チェック（毎フレーム呼ぶ）
    void Update();

    bool IsEnabled() const  { return enabled_; }
    void SetEnabled(bool e) { enabled_ = e; }

private:
    ShaderHotReload()  = default;
    ~ShaderHotReload() = default;
    ShaderHotReload(const ShaderHotReload&)            = delete;
    ShaderHotReload& operator=(const ShaderHotReload&) = delete;

    struct Entry {
        uint32_t                              id;
        std::wstring                          path;
        const wchar_t*                        profile;
        std::function<void(IDxcBlob*)>        callback;
        std::filesystem::file_time_type       lastWriteTime;
        Microsoft::WRL::ComPtr<IDxcBlob>      lastBlob;
    };

    engine::DirectXCommon*       dxCommon_ = nullptr;
    bool                 enabled_  = true;
    uint32_t             nextId_   = 0;
    std::vector<Entry>   entries_;

    // GPU が前フレームのシェーダーを使い終わるまで古い blob を保持するための遅延解放キュー
    // pair: {フレームカウンタ, blob}
    uint64_t                                                    frameCount_ = 0;
    std::vector<std::pair<uint64_t, Microsoft::WRL::ComPtr<IDxcBlob>>> pendingRelease_;
};

} // namespace engine::graphics
