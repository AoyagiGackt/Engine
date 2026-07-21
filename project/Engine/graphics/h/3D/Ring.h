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
        /**
         * @brief RebuildVertices に対応する処理を実行する
         * @return 処理結果
         */
        RebuildVertices();
    }
    void SetOuterRadius(float R)
    {
        outerRadius_ = R;
        /**
         * @brief RebuildVertices に対応する処理を実行する
         * @return 処理結果
         */
        RebuildVertices();
    }

protected:
    void SetWVP(const Matrix4x4& wvp) override { materialData_->WVP = wvp; }

private:
    void CreatePipeline();
    void RebuildVertices();

    /**
     * @brief MaterialCB に関する型を提供する
     * @details MaterialCB が扱うデータと操作の責務をまとめる
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
