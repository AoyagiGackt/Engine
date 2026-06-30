#include "FontRenderer.h"
#include "TextureManager.h"
#define NOMINMAX
#include <windows.h>
#include <cassert>
#include <cstring>
#include <vector>
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

static constexpr const char* kAtlasKey   = "__fontAtlas__";
static constexpr const char* kJpAtlasKey = "__fontAtlasJp__";

// ひらがな 0x3041-0x3096 (86文字), カタカナ 0x30A0-0x30FF (96文字) は範囲カバー
// それ以外でゲームUIに使う文字を追加
static const wchar_t kJpExtra[] =
    L"覚醒中発動鬼神銃士奇術師守護者射撃段斬★"  // 既存
    L"格闘連玉"                                    // 武器UI
    L"武器操作説明"                                // TrainingScene
    L"戦強敵休憩"                                  // マップノード
    L"倒獲得報酬高多選取永続効果回復最終決全力挑択定" // マップ説明
    L"化延長速促進疾走跳躍乱舞維持"               // スキル名
    L"距離倍大数階増加弾度蓄積移減衰"             // スキル説明
    L"所済次開始"                                  // ショップ・タイトル
    L"切替散";                                     // TrainingScene追加
static constexpr uint32_t kHiraganaStart  = 0x3041;
static constexpr uint32_t kHiraganaEnd    = 0x3096;
static constexpr uint32_t kKatakanaStart  = 0x30A0;
static constexpr uint32_t kKatakanaEnd    = 0x30FF;
static constexpr int      kHiraganaCount  = static_cast<int>(kHiraganaEnd - kHiraganaStart + 1); // 86
static constexpr int      kKatakanaCount  = static_cast<int>(kKatakanaEnd - kKatakanaStart + 1); // 96
static constexpr int      kKanaTotal      = kHiraganaCount + kKatakanaCount;                      // 182

void FontRenderer::BuildAtlas()
{
    if (TextureManager::GetInstance()->HasTexture(kAtlasKey)) { return; }

    // GDI で Courier New を 32bit DIBSection に描画する
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = kAtlasW;
    bmi.bmiHeader.biHeight      = -kAtlasH;    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP hBmp = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    assert(hBmp && bits);

    HDC hdcMem = CreateCompatibleDC(nullptr);
    HBITMAP hOld = static_cast<HBITMAP>(SelectObject(hdcMem, hBmp));

    // 背景を黒で塗りつぶす
    memset(bits, 0, static_cast<size_t>(kAtlasW) * kAtlasH * 4);

    // フォント: Courier New, 高さ -13px（文字高さ指定）
    LOGFONTA lf{};
    lf.lfHeight         = -13;
    lf.lfWeight         = FW_NORMAL;
    lf.lfCharSet        = ANSI_CHARSET;
    lf.lfOutPrecision   = OUT_TT_PRECIS;
    lf.lfClipPrecision  = CLIP_DEFAULT_PRECIS;
    lf.lfQuality        = ANTIALIASED_QUALITY;
    lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
    strcpy_s(lf.lfFaceName, "Courier New");

    HFONT hFont = CreateFontIndirectA(&lf);
    assert(hFont);
    HFONT hOldFont = static_cast<HFONT>(SelectObject(hdcMem, hFont));

    SetBkMode(hdcMem, TRANSPARENT);
    SetTextColor(hdcMem, RGB(255, 255, 255));

    // ASCII 32〜127 を atlas に描く
    char buf[2] = { 0, 0 };
    for (int c = kCharBase; c < kCharBase + kCols * kRows; ++c) {
        int idx = c - kCharBase;
        int col = idx % kCols;
        int row = idx / kCols;
        buf[0]  = static_cast<char>(c);
        TextOutA(hdcMem, col * kCharW, row * kCharH + 1, buf, 1);
    }

    GdiFlush();
    SelectObject(hdcMem, hOldFont);
    SelectObject(hdcMem, hOld);
    DeleteObject(hFont);
    DeleteDC(hdcMem);

    // BGRA（GDI DIB）→ RGBA（DX12用）変換
    // 白テキスト on 黒背景なので輝度をアルファに使う
    const auto* src = static_cast<const uint8_t*>(bits);
    std::vector<uint8_t> rgba(static_cast<size_t>(kAtlasW) * kAtlasH * 4);
    for (int i = 0; i < kAtlasW * kAtlasH; ++i) {
        uint8_t r = src[i * 4 + 2];
        uint8_t g = src[i * 4 + 1];
        uint8_t b = src[i * 4 + 0];
        uint8_t a = static_cast<uint8_t>((static_cast<int>(r) + g + b) / 3);
        rgba[i * 4 + 0] = 255;
        rgba[i * 4 + 1] = 255;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = a;
    }

    DeleteObject(hBmp);

    TextureManager::GetInstance()->LoadFromRawRGBA8(kAtlasKey, rgba.data(), kAtlasW, kAtlasH);
    // FlushUploads は SceneManager がシーン初期化後に一括で行う
}

// ──────────────────────────────────────────────────────────────────────
// JP アトラス：ひらがな・カタカナ・指定漢字を 16x16 グリッドで焼く
// ──────────────────────────────────────────────────────────────────────

int FontRenderer::GetJpGlyphIdx(wchar_t c) const
{
    uint32_t u = static_cast<uint32_t>(c);
    if (u >= kHiraganaStart && u <= kHiraganaEnd) {
        return static_cast<int>(u - kHiraganaStart);
    }
    if (u >= kKatakanaStart && u <= kKatakanaEnd) {
        return kHiraganaCount + static_cast<int>(u - kKatakanaStart);
    }
    for (int i = 0; kJpExtra[i]; ++i) {
        if (static_cast<wchar_t>(kJpExtra[i]) == c) { return kKanaTotal + i; }
    }
    return -1;
}

void FontRenderer::BuildJpAtlas()
{
    if (TextureManager::GetInstance()->HasTexture(kJpAtlasKey)) { return; }

    int extraCount  = static_cast<int>(wcslen(kJpExtra));
    int totalGlyphs = kKanaTotal + extraCount;
    jpAtlasRows_    = (totalGlyphs + kJpCols - 1) / kJpCols;
    int atlasW      = kJpCols * kJpCharW;            // 256
    int atlasH      = jpAtlasRows_ * kJpCharH;

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = atlasW;
    bmi.bmiHeader.biHeight      = -atlasH;              bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void*   bits = nullptr;
    HBITMAP hBmp = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    assert(hBmp && bits);

    HDC     hdcMem = CreateCompatibleDC(nullptr);
    HBITMAP hOld   = static_cast<HBITMAP>(SelectObject(hdcMem, hBmp));
    memset(bits, 0, static_cast<size_t>(atlasW) * atlasH * 4);

    LOGFONTW lf{};
    lf.lfHeight         = -(kJpCharH - 2);
    lf.lfWeight         = FW_NORMAL;
    lf.lfCharSet        = DEFAULT_CHARSET;
    lf.lfOutPrecision   = OUT_TT_PRECIS;
    lf.lfQuality        = ANTIALIASED_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    wcscpy_s(lf.lfFaceName, L"MS Gothic");

    HFONT hFont = CreateFontIndirectW(&lf);
    assert(hFont);
    HFONT hOldFont = static_cast<HFONT>(SelectObject(hdcMem, hFont));

    SetBkMode(hdcMem, TRANSPARENT);
    SetTextColor(hdcMem, RGB(255, 255, 255));

    wchar_t wbuf[2] = { 0, 0 };
    auto renderAt = [&](int idx, wchar_t ch) {
        wbuf[0]   = ch;
        int col   = idx % kJpCols;
        int row   = idx / kJpCols;
        TextOutW(hdcMem, col * kJpCharW, row * kJpCharH, wbuf, 1);
    };

    for (int i = 0; i < kHiraganaCount; ++i) {
        renderAt(i, static_cast<wchar_t>(kHiraganaStart + i));
    }
    for (int i = 0; i < kKatakanaCount; ++i) {
        renderAt(kHiraganaCount + i, static_cast<wchar_t>(kKatakanaStart + i));
    }
    for (int i = 0; kJpExtra[i]; ++i) {
        renderAt(kKanaTotal + i, kJpExtra[i]);
    }

    GdiFlush();
    SelectObject(hdcMem, hOldFont);
    SelectObject(hdcMem, hOld);
    DeleteObject(hFont);
    DeleteDC(hdcMem);

    // BGRA（GDI DIB）→ RGBA / 輝度をアルファに
    const auto* src = static_cast<const uint8_t*>(bits);
    std::vector<uint8_t> rgba(static_cast<size_t>(atlasW) * atlasH * 4);
    for (int i = 0; i < atlasW * atlasH; ++i) {
        uint8_t r = src[i * 4 + 2];
        uint8_t g = src[i * 4 + 1];
        uint8_t b = src[i * 4 + 0];
        uint8_t a = static_cast<uint8_t>((static_cast<int>(r) + g + b) / 3);
        rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = a;
    }
    DeleteObject(hBmp);

    TextureManager::GetInstance()->LoadFromRawRGBA8(kJpAtlasKey, rgba.data(), atlasW, atlasH);
    // FlushUploads は SceneManager がシーン初期化後に一括で行う
}

void FontRenderer::Initialize(SpriteCommon* spriteCommon)
{
    spriteCommon_ = spriteCommon;
    BuildAtlas();
    BuildJpAtlas();

    sprites_.resize(kMaxChars);
    for (auto& s : sprites_)   { s.Initialize(spriteCommon_, kAtlasKey); }

    jpSprites_.resize(kMaxChars);
    for (auto& s : jpSprites_) { s.Initialize(spriteCommon_, kJpAtlasKey); }
}

void FontRenderer::DrawString(const std::string& text, float x, float y,
    float scale, const Vector4& color)
{
    cmds_.push_back({ text, x, y, scale, color });
}

void FontRenderer::DrawStringW(const std::wstring& text, float x, float y,
    float scale, const Vector4& color)
{
    cmdsW_.push_back({ text, x, y, scale, color });
}

void FontRenderer::Reset()
{
    cmds_.clear();
    cmdsW_.clear();
    spriteIdx_   = 0;
    jpSpriteIdx_ = 0;
}

void FontRenderer::Draw()
{
    // ── ASCII 文字列 ──────────────────────────────────────────────────
    for (const auto& cmd : cmds_) {
        float cx = cmd.x;
        for (unsigned char c : cmd.text) {
            if (spriteIdx_ >= kMaxChars) { return; }
            int idx = static_cast<int>(c) - kCharBase;
            if (idx < 0 || idx >= kCols * kRows) { cx += kCharW * cmd.scale; continue; }
            int col = idx % kCols;
            int row = idx / kCols;
            auto& s = sprites_[spriteIdx_++];
            s.SetPosition({ cx, cmd.y });
            s.SetSize({ (float)kCharW * cmd.scale, (float)kCharH * cmd.scale });
            s.SetTextureLeftTop({ (float)(col * kCharW), (float)(row * kCharH) });
            s.SetTextureSize({ (float)kCharW, (float)kCharH });
            s.SetColor(cmd.color);
            s.Update();
            s.Draw();
            cx += kCharW * cmd.scale;
        }
    }

    // ── 日本語（ASCII 混在可）ワイド文字列 ────────────────────────────
    for (const auto& cmd : cmdsW_) {
        float cx = cmd.x;
        for (wchar_t wc : cmd.text) {
            if (wc < 128) {
                // ASCII 部分 → ASCII アトラス
                int idx = static_cast<int>(wc) - kCharBase;
                if (idx >= 0 && idx < kCols * kRows && spriteIdx_ < kMaxChars) {
                    int col = idx % kCols;
                    int row = idx / kCols;
                    auto& s = sprites_[spriteIdx_++];
                    s.SetPosition({ cx, cmd.y });
                    s.SetSize({ (float)kCharW * cmd.scale, (float)kCharH * cmd.scale });
                    s.SetTextureLeftTop({ (float)(col * kCharW), (float)(row * kCharH) });
                    s.SetTextureSize({ (float)kCharW, (float)kCharH });
                    s.SetColor(cmd.color);
                    s.Update();
                    s.Draw();
                }
                cx += kCharW * cmd.scale;
            } else {
                // 日本語 → JP アトラス
                int jpIdx = GetJpGlyphIdx(wc);
                if (jpIdx >= 0 && jpSpriteIdx_ < kMaxChars) {
                    int col = jpIdx % kJpCols;
                    int row = jpIdx / kJpCols;
                    auto& s = jpSprites_[jpSpriteIdx_++];
                    s.SetPosition({ cx, cmd.y });
                    s.SetSize({ (float)kJpCharW * cmd.scale, (float)kJpCharH * cmd.scale });
                    s.SetTextureLeftTop({ (float)(col * kJpCharW), (float)(row * kJpCharH) });
                    s.SetTextureSize({ (float)kJpCharW, (float)kJpCharH });
                    s.SetColor(cmd.color);
                    s.Update();
                    s.Draw();
                }
                cx += kJpCharW * cmd.scale;
            }
        }
    }
}
