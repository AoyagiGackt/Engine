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
    // HUDの文字に影を重ねて描く箇所が増え、1文字あたり2スプライト消費するようになったため、
    // 512のままだと1フレームの文字数上限に達して途中から文字が消える(描画スキップされる)ようになった。
    // ASCII/JPそれぞれのプールに余裕を持たせておく
    static constexpr int kMaxChars = 2048;

    // JP アトラス定数（ひらがな・カタカナ・漢字）
    static constexpr int kJpCharW = 16;
    static constexpr int kJpCharH = 16;
    static constexpr int kJpCols = 16;

    // Regular（非Bold）用スプライトプール文字数上限UIテキスト等の短いラベル用途を想定し、
    // 常時大量に使われるBold側(kMaxChars)より小さい枠で確保する
    static constexpr int kMaxRegularChars = 512;

    /**
     * @brief ASCII/JP各2種(Bold/Regular)のアトラスを構築（未生成なら）し、描画用スプライトプールを確保する
     * @param spriteCommon スプライト描画の共通設定
     */
    void Initialize(SpriteCommon* spriteCommon);

    // ASCII 文字列描画（bold=falseでRegularウェイトを使う）
    void DrawString(const std::string& text, float x, float y,
        float scale = 1.0f,
        const Vector4& color = { 1.0f, 1.0f, 1.0f, 1.0f },
        bool bold = true);

    // 日本語（ひらがな・カタカナ・漢字）+ ASCII 混在文字列描画（bold=falseでRegularウェイトを使う）
    void DrawStringW(const std::wstring& text, float x, float y,
        float scale = 1.0f,
        const Vector4& color = { 1.0f, 1.0f, 1.0f, 1.0f },
        bool bold = true);

    /** @brief 積んだ描画コマンドとスプライト使用数をクリアする（毎フレーム、DrawString系を呼ぶ前に呼ぶ） */
    void Reset();
    /** @brief Reset()以降に積んだASCII/JP文字列コマンドをすべてスプライトとして描画する */
    void Draw();

private:
    /** @brief Draw()まで遅延させるASCII文字列描画コマンド1件（文字列・座標・スケール・色・太字指定） */
    struct DrawCmd {
        std::string text;
        float x, y, scale;
        Vector4 color;
        bool bold = true;
    };
    /** @brief Draw()まで遅延させる日本語文字列描画コマンド1件（文字列・座標・スケール・色・太字指定） */
    struct DrawCmdW {
        std::wstring text;
        float x, y, scale;
        Vector4 color;
        bool bold = true;
    };

    /** @brief GDIでCourier NewのASCII文字をDIBSectionへ描画し、ASCIIアトラステクスチャとして登録する（登録済みなら何もしない） */
    void BuildAtlas(bool bold);
    /** @brief ひらがな・カタカナ・追加漢字グリフをGDIで描画し、JPアトラステクスチャとして登録する（登録済みなら何もしない） */
    void BuildJpAtlas(bool bold);
    /**
     * @brief 文字がJPアトラス内のどのグリフ番号に対応するかを返す
     * @param c 判定する文字（ひらがな/カタカナ/kJpExtra内の追加漢字）
     * @return グリフ番号（アトラス内の並び順）対応外の文字なら-1
     */
    int GetJpGlyphIdx(wchar_t c) const;

    SpriteCommon* spriteCommon_ = nullptr;
    std::vector<Sprite> sprites_; // Bold ASCII
    std::vector<Sprite> spritesRegular_; // Regular ASCII
    std::vector<DrawCmd> cmds_;
    int spriteIdx_ = 0;
    int spriteRegularIdx_ = 0;

    std::vector<Sprite> jpSprites_; // Bold JP
    std::vector<Sprite> jpSpritesRegular_; // Regular JP
    std::vector<DrawCmdW> cmdsW_;
    int jpSpriteIdx_ = 0;
    int jpSpriteRegularIdx_ = 0;
    int jpAtlasRows_ = 0;
    int jpAtlasRowsRegular_ = 0;
};

} // namespace engine::game
