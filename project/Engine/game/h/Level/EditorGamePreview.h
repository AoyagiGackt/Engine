/**
 * @file EditorGamePreview.h
 * @brief エディタ内のゲーム小窓に表示するプレビュー用リソースを管理するクラスを定義するファイル
 */
#pragma once
#ifdef USE_IMGUI
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#endif
namespace engine { class DirectXCommon; }
namespace engine::game {
class StageEditor;
/** @brief ゲーム小窓用のコピー先とSRVを所有し、エディタへ表示だけを渡す。 */
class EditorGamePreview {
public:
    void Prepare(engine::DirectXCommon& graphics, StageEditor& editor);
    void Capture(engine::DirectXCommon& graphics, const StageEditor& editor);
    // 呼び出し前にGPU完了を待つ。
    void Finalize();
private:
#ifdef USE_IMGUI
    Microsoft::WRL::ComPtr<ID3D12Resource> texture_;
    uint32_t srv_ = 0;
    bool allocated_ = false;
#endif
};
}
