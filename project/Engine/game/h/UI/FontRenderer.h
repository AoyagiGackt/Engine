/**
 * @file FontRenderer.h
 * @brief ASCII・日本語（ひらがな/カタカナ/漢字）のビットマップフォント描画を行うファイル
 */
#pragma once
#include "Sprite.h"
#include "SpriteCommon.h"
#include <string>
#include <vector>
namespace engine::game {
using engine::graphics::Sprite;
using engine::graphics::SpriteCommon;

/**
 * @brief コード生成したビットマップフォントアトラスを使い、文字列をスプライトで描画するクラス
 * @note ASCII用とJP用の2種類のアトラスを内部で生成し、DrawString/DrawStringWで描き分ける
 */
class FontRenderer {
public:
    // ASCII アトラス定数
    static constexpr int kCharW = 8;
    static constexpr int kCharH = 16;
    static constexpr int kCols = 16;
    static constexpr int kRows = 6;
    static constexpr int kAtlasW = kCharW * kCols; // 128
    static constexpr int kAtlasH = kCharH * kRows; // 96
    static constexpr int kCharBase = 32;
    static constexpr int kMaxChars = 512;

    // JP アトラス定数（ひらがな・カタカナ・漢字）
    static constexpr int kJpCharW = 16;
    static constexpr int kJpCharH = 16;
    static constexpr int kJpCols = 16;

    /**
     * @brief ASCII/JP両アトラスを構築（未生成なら）し、描画用スプライトプールを確保する
     * @param spriteCommon スプライト描画の共通設定
     */
    void Initialize(SpriteCommon* spriteCommon);

    // ASCII 文字列描画
    void DrawString(const std::string& text, float x, float y,
        float scale = 1.0f,
        const Vector4& color = { 1.0f, 1.0f, 1.0f, 1.0f });

    // 日本語（ひらがな・カタカナ・漢字）+ ASCII 混在文字列描画
    void DrawStringW(const std::wstring& text, float x, float y,
        float scale = 1.0f,
        const Vector4& color = { 1.0f, 1.0f, 1.0f, 1.0f });

    /** @brief 積んだ描画コマンドとスプライト使用数をクリアする（毎フレーム、DrawString系を呼ぶ前に呼ぶ） */
    void Reset();
    /** @brief Reset()以降に積んだASCII/JP文字列コマンドをすべてスプライトとして描画する */
    void Draw();

private:
    /** @brief Draw()まで遅延させるASCII文字列描画コマンド1件（文字列・座標・スケール・色） */
    struct DrawCmd {
        std::string text;
        float x, y, scale;
        Vector4 color;
    };
    /** @brief Draw()まで遅延させる日本語文字列描画コマンド1件（文字列・座標・スケール・色） */
    struct DrawCmdW {
        std::wstring text;
        float x, y, scale;
        Vector4 color;
    };

    /** @brief GDIでCourier Newの全ASCII文字をDIBSectionへ描画し、ASCIIアトラステクスチャとして登録する（登録済みなら何もしない） */
    void BuildAtlas();
    /** @brief ひらがな・カタカナ・追加漢字グリフをGDIで描画し、JPアトラステクスチャとして登録する（登録済みなら何もしない） */
    void BuildJpAtlas();
    /**
     * @brief 文字がJPアトラス内のどのグリフ番号に対応するかを返す
     * @param c 判定する文字（ひらがな/カタカナ/kJpExtra内の追加漢字）
     * @return グリフ番号（アトラス内の並び順）対応外の文字なら-1
     */
    int GetJpGlyphIdx(wchar_t c) const;

    SpriteCommon* spriteCommon_ = nullptr;
    std::vector<Sprite> sprites_;
    std::vector<DrawCmd> cmds_;
    int spriteIdx_ = 0;

    std::vector<Sprite> jpSprites_;
    std::vector<DrawCmdW> cmdsW_;
    int jpSpriteIdx_ = 0;
    int jpAtlasRows_ = 0;
};

} // namespace engine::game
