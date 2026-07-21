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
     * @brief Initialize に対応する処理を開始する
     * @param spriteCommon 処理に使用する値
     * @return なし
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

    /**
     * @brief Reset に対応する状態を設定する
     * @return なし
     */
    void Reset();
    /**
     * @brief Draw に対応する内容を描画する
     * @return なし
     */
    void Draw();

private:
    /**
     * @brief DrawCmd に関する型を提供する
     * @details DrawCmd が扱うデータと操作の責務をまとめる
     */
    struct DrawCmd {
        std::string text;
        float x, y, scale;
        Vector4 color;
    };
    /**
     * @brief DrawCmdW に関する型を提供する
     * @details DrawCmdW が扱うデータと操作の責務をまとめる
     */
    struct DrawCmdW {
        std::wstring text;
        float x, y, scale;
        Vector4 color;
    };

    /**
     * @brief BuildAtlas に対応する処理を実行する
     * @return なし
     */
    void BuildAtlas();
    /**
     * @brief BuildJpAtlas に対応する処理を実行する
     * @return なし
     */
    void BuildJpAtlas();
    /**
     * @brief GetJpGlyphIdx の結果を取得する
     * @param c 処理に使用する値
     * @return 処理結果
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
