#include "ShaderHotReload.h"
#include <cassert>

void ShaderHotReload::Initialize(DirectXCommon* dxCommon)
{
    assert(dxCommon);
    dxCommon_ = dxCommon;
}

uint32_t ShaderHotReload::Register(const std::wstring& path, const wchar_t* profile,
    std::function<void(IDxcBlob*)> onRecompiled)
{
    Entry e;
    e.id       = nextId_++;
    e.path     = path;
    e.profile  = profile;
    e.callback = std::move(onRecompiled);

    // 初回コンパイルしてタイムスタンプを記録
    try {
        e.lastWriteTime = std::filesystem::last_write_time(path);
    } catch (...) {
        e.lastWriteTime = {};
    }
    e.lastBlob = dxCommon_->CompileShader(path, profile);

    uint32_t id = e.id;
    entries_.push_back(std::move(e));
    return id;
}

void ShaderHotReload::Unregister(uint32_t id)
{
    auto it = std::find_if(entries_.begin(), entries_.end(),
        [id](const Entry& e) { return e.id == id; });
    if (it != entries_.end()) entries_.erase(it);
}

void ShaderHotReload::Update()
{
    if (!enabled_) return;

    ++frameCount_;

    // 2フレーム前以前の解放待ち blob を解放（ダブルバッファリングに対応）
    pendingRelease_.erase(
        std::remove_if(pendingRelease_.begin(), pendingRelease_.end(),
            [this](const std::pair<uint64_t, Microsoft::WRL::ComPtr<IDxcBlob>>& p) {
                return p.first + 2 <= frameCount_;
            }),
        pendingRelease_.end());

    for (auto& e : entries_) {
        std::filesystem::file_time_type wt;
        try {
            wt = std::filesystem::last_write_time(e.path);
        } catch (...) {
            continue;
        }
        if (wt <= e.lastWriteTime) continue;

        IDxcBlob* newBlob = dxCommon_->CompileShader(e.path, e.profile);
        if (!newBlob) continue; // コンパイルエラーは無視して古いシェーダーを使い続ける

        e.lastWriteTime = wt;

        // 古い blob は遅延解放キューへ
        if (e.lastBlob) {
            pendingRelease_.push_back({ frameCount_, std::move(e.lastBlob) });
        }
        e.lastBlob = newBlob;

        // GPU が古い PSO を参照中に破棄されないよう、コールバック前にフェンス待機
        {
            auto* queue   = dxCommon_->GetCommandQueue();
            auto* fence   = dxCommon_->GetFence();
            dxCommon_->IncrementFenceValue();
            uint64_t waitVal = dxCommon_->GetFenceValue();
            queue->Signal(fence, waitVal);
            if (fence->GetCompletedValue() < waitVal) {
                fence->SetEventOnCompletion(waitVal, dxCommon_->GetFenceEvent());
                WaitForSingleObject(dxCommon_->GetFenceEvent(), INFINITE);
            }
        }
        e.callback(newBlob);
    }
}
