/**
 * @file Cylinder.h
 * @brief Cylinderの描画資源とGPU処理の管理に関する公開型と操作インターフェースを定義するファイル
 */
#pragma once
#include "RevolvedMeshBase.h"

namespace engine::graphics {

class Cylinder : public RevolvedMeshBase {
public:
    void Initialize(engine::DirectXCommon* dxCommon,
        const std::string& textureFilePath = "Resources/gradationLine.png",
        int divisions = 32);

    void SetColor(const Vector4& color) { materialData_->color = color; }
    void SetTopRadius(float r)
    {
        topRadius_ = r;
        /**
         * @brief RebuildVertices に対応する処理を実行する
         * @return 処理結果
         */
        RebuildVertices();
    }
    void SetBottomRadius(float r)
    {
        bottomRadius_ = r;
        /**
         * @brief RebuildVertices に対応する処理を実行する
         * @return 処理結果
         */
        RebuildVertices();
    }
    void SetHeight(float h)
    {
        height_ = h;
        /**
         * @brief RebuildVertices に対応する処理を実行する
         * @return 処理結果
         */
        RebuildVertices();
    }
    void SetAlphaReference(float a) { materialData_->alphaReference = a; }

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
        float alphaReference;
        float _pad[3];
    };

    MaterialCB* materialData_ = nullptr;

    float topRadius_ = 1.0f;
    float bottomRadius_ = 1.0f;
    float height_ = 3.0f;
};

} // namespace engine::graphics
