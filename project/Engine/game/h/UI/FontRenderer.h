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
    // （プールは使った分だけ伸びるので、上限を上げても普段のメモリは増えない）
    static constexpr int kMaxChars = 4096;

    // JP アトラス定数（ひらがな・カタカナ・漢字）
    static constexpr int kJpCharW = 16;
    static constexpr int kJpCharH = 16;
    static constexpr int kJpCols = 64; // JIS第二水準まで焼くため横長にして縦の行数を抑える

    // Regular（非Bold）用スプライトプール文字数上限
    // 案内文や警告が重なると512では足りずに文字が抜けたため、Bold側と同じ桁まで上げている
    static constexpr int kMaxRegularChars = 2048;

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

    /**
     * @brief 文字の下に敷く単色の板を積む（Draw()で文字より先に描く）
     * @note 明るい背景の上でも案内文が読めるようにするための下敷き
     */
    void DrawPanel(float x, float y, float width, float height, const Vector4& color);

    /**
     * @brief DrawStringWで描いた時の大きさを返す（改行は行数として数える）
     * @return x=最も長い行の幅、y=全行の高さ
     */
    static Vector2 MeasureStringW(const std::wstring& text, float scale);

    /** @brief 積んだ描画コマンドとスプライト使用数をクリアする（毎フレーム、DrawString系を呼ぶ前に呼ぶ） */
    void Reset();
    /** @brief Reset()以降に積んだASCII/JP文字列コマンドをすべてスプライトとして描画する */
    void Draw();

private:
    /** @brief 必要な文字数に応じてスプライトを追加し、既存のものは再利用する */
    Sprite& AcquireGlyphSprite(std::vector<Sprite>& pool, int& index, const char* atlas);
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
     * @param c 判定する文字（ひらがな/カタカナ/全角記号/第一水準漢字/kJpExtra内の追加文字）
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

    /** @brief Draw()まで遅延させる下敷きの板1枚 */
    struct PanelCmd {
        float x, y, width, height;
        Vector4 color;
    };
    std::vector<PanelCmd> panelCmds_;
    std::vector<Sprite> panelSprites_;
    int panelSpriteIdx_ = 0;
};

} // namespace engine::game
