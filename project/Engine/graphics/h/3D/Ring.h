/**
 * @file Ring.h
 * @brief Ringの描画資源とGPU処理の管理に関する公開型と操作インターフェースを定義するファイル
 */
#pragma once
#include "RevolvedMeshBase.h"

namespace engine::graphics {

class Ring : public RevolvedMeshBase {
public:
    void Initialize(engine::DirectXCommon* dxCommon,
        const std::string& textureFilePath = "Resources/gradationLine.png",
        int divisions = 32);

    void SetColor(const Vector4& color) { materialData_->color = color; }
    void SetInnerRadius(float r)
    {
        innerRadius_ = r;
        // 半径変更を反映して頂点データを再生成する
        RebuildVertices();
    }
    void SetOuterRadius(float R)
    {
        outerRadius_ = R;
        // 半径変更を反映して頂点データを再生成する
        RebuildVertices();
    }

protected:
    void SetWVP(const Matrix4x4& wvp) override { materialData_->WVP = wvp; }

private:
    void CreatePipeline();
    void RebuildVertices();

    /**
     * @brief Ring描画PS/VS用の定数バッファに1:1で対応するパラメータ構造体
     */
    struct MaterialCB {
        Matrix4x4 WVP;
        Vector4 color;
    };

    MaterialCB* materialData_ = nullptr;

    float innerRadius_ = 1.0f;
    float outerRadius_ = 2.0f;
};

} // namespace engine::graphics
