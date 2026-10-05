/**
 * @file FontRenderer.cpp
 * @brief ビットマップフォントアトラス生成とASCII/日本語文字列描画（FontRenderer）の実装
 */
#include "FontRenderer.h"
#include "TextureManager.h"
#define NOMINMAX
#include "EngineAssert.h"
#include <cstring>
#include <unordered_map>
#include <vector>
#include <windows.h>
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

static constexpr const char* kAtlasKey = "__fontAtlas__";
static constexpr const char* kAtlasKeyRegular = "__fontAtlasRegular__";
static constexpr const char* kJpAtlasKey = "__fontAtlasJp__";
static constexpr const char* kJpAtlasKeyRegular = "__fontAtlasJpRegular__";

// ひらがな 0x3041-0x3096 (86文字), カタカナ 0x30A0-0x30FF (96文字) は範囲カバー
// Shift_JIS範囲（後述）に含まれない文字でゲームUIに使うものを追加（範囲内の文字は重複しても無視される）
static const wchar_t kJpExtra[] = L"覚醒中発動鬼神銃士奇術師守護者射撃段斬★" // 既存
                                  L"格闘連玉" // 武器UI
                                  L"武器操作説明" // TrainingScene
                                  L"戦強敵休憩" // マップノード
                                  L"倒獲得報酬高多選取永続効果回復最終決全力挑択定" // マップ説明
                                  L"化延長速促進疾走跳躍乱舞維持" // スキル名
                                  L"距離倍大数階増加弾度蓄積移減衰" // スキル説明
                                  L"所済次開始" // ショップ・タイトル
                                  L"切替散" // TrainingScene追加
                                  L"満打上空手戻重騎死狂突掬剛叩落剣下刈魂斧" // 武器コマンド・スタイル名・マップ説明の不足分
                                  L"締踏込貫通" // 銃コンボのコマンド説明
                                  L"了交体備入前区口合固壁壊変外奪完左技接換攻有杯棄槍画直瞬破練装解訓赤迅間障青"
                                  L"寄押" // ロックオンUI（最寄り・長押し）
                                  L"収避無身可割価待構評吸" // 回避/無敵・武器吸収・マップ評価UIの不足分
                                  L"空中追撃段別推奨順番" // コンボルートガイド
                                  L"擲" // レベル2の案内文（JIS第二水準のため範囲外）
                                  // 以下はShift_JIS範囲にも含まれるが、範囲の一括追加を外しても画面の文章が欠けないよう明示しておく
                                  L"→○●　、。々（）：？" // HUD・マップ・ショップ・案内文の記号
                                  L"一予二先光内円再出制削告呼囲型基場実寸屋崩帰常広床役径後必快扉投抜探揺文時来桁案機正殺気波消渡渦準"
                                  L"溜滅滞演灰特牽生目着短秒程立第箱範約紙索緑能色荒見読起超足車転軽輪近退逆遅違遠還部量面駐"; // 武器説明・レベル案内文・グラフの文章
static constexpr uint32_t kHiraganaStart = 0x3041;
static constexpr uint32_t kHiraganaEnd = 0x3096;
static constexpr uint32_t kKatakanaStart = 0x30A0;
static constexpr uint32_t kKatakanaEnd = 0x30FF;
static constexpr int kHiraganaCount = static_cast<int>(kHiraganaEnd - kHiraganaStart + 1); // 86
static constexpr int kKatakanaCount = static_cast<int>(kKatakanaEnd - kKatakanaStart + 1); // 96

// Shift_JIS の全角記号・英数字・JIS第一水準漢字（0x8140-0x9872）をまとめて焼き込み、
// レベルデータ等で自由に書かれた文章でも文字が欠けないようにする
static constexpr UINT kShiftJisCodePage = 932;
static constexpr int kSjisLeadFirst = 0x81;
static constexpr int kSjisLeadLast = 0x98;
static constexpr int kSjisTrailFirst = 0x40;
static constexpr int kSjisTrailLast = 0xFC;
static constexpr int kSjisTrailInvalid = 0x7F;
static constexpr int kSjisLastCode = 0x9872;
static constexpr int kSjisByteShift = 8;
static constexpr uint32_t kAsciiLimit = 128;

namespace {
/** @brief JPアトラスに焼く文字の並び（かな→Shift_JIS範囲→kJpExtraの残り）と文字→グリフ番号の対応表 */
struct JpGlyphTable {
    std::vector<wchar_t> glyphs;
    std::unordered_map<wchar_t, int> indexOf;

    void Add(wchar_t ch)
    {
        if (static_cast<uint32_t>(ch) < kAsciiLimit || indexOf.count(ch)) {
            return;
        }
        indexOf.emplace(ch, static_cast<int>(glyphs.size()));
        glyphs.push_back(ch);
    }
};

const JpGlyphTable& GetJpGlyphTable()
{
    static const JpGlyphTable table = [] {
        JpGlyphTable t;
        for (int i = 0; i < kHiraganaCount; ++i) {
            t.Add(static_cast<wchar_t>(kHiraganaStart + i));
        }
        for (int i = 0; i < kKatakanaCount; ++i) {
            t.Add(static_cast<wchar_t>(kKatakanaStart + i));
        }
        for (int lead = kSjisLeadFirst; lead <= kSjisLeadLast; ++lead) {
            for (int trail = kSjisTrailFirst; trail <= kSjisTrailLast; ++trail) {
                if (trail == kSjisTrailInvalid || ((lead << kSjisByteShift) | trail) > kSjisLastCode) {
                    continue;
                }
                const char bytes[2] = { static_cast<char>(lead), static_cast<char>(trail) };
                wchar_t wc = 0;
                if (MultiByteToWideChar(kShiftJisCodePage, MB_ERR_INVALID_CHARS, bytes, 2, &wc, 1) == 1) {
                    t.Add(wc);
                }
            }
        }
        for (int i = 0; kJpExtra[i]; ++i) {
            t.Add(kJpExtra[i]);
        }
        return t;
    }();
    return table;
}
} // namespace

void FontRenderer::BuildAtlas(bool bold)
{
    const char* atlasKey = bold ? kAtlasKey : kAtlasKeyRegular;
    if (TextureManager::GetInstance()->HasTexture(atlasKey)) {
        return;
    }

    // GDI で Courier New を 32bit DIBSection に描画する
    BITMAPINFO bmi { };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = kAtlasW;
    bmi.bmiHeader.biHeight = -kAtlasH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP hBmp = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ENGINE_ASSERT(hBmp && bits);

    HDC hdcMem = CreateCompatibleDC(nullptr);
    HBITMAP hOld = static_cast<HBITMAP>(SelectObject(hdcMem, hBmp));

    // 背景を黒で塗りつぶす
    memset(bits, 0, static_cast<size_t>(kAtlasW) * kAtlasH * 4);

    // フォント  Courier New, 高さ -13px（文字高さ指定）
    LOGFONTA lf { };
    lf.lfHeight = -13;
    lf.lfWeight = bold ? FW_BOLD : FW_NORMAL;
    lf.lfCharSet = ANSI_CHARSET;
    lf.lfOutPrecision = OUT_TT_PRECIS;
    lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lf.lfQuality = ANTIALIASED_QUALITY;
    lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
    strcpy_s(lf.lfFaceName, "Courier New");

    HFONT hFont = CreateFontIndirectA(&lf);
    ENGINE_ASSERT(hFont);
    HFONT hOldFont = static_cast<HFONT>(SelectObject(hdcMem, hFont));

    SetBkMode(hdcMem, TRANSPARENT);
    SetTextColor(hdcMem, RGB(255, 255, 255));

    // ASCII 32〜127 を atlas に描く
    char buf[2] = { 0, 0 };
    for (int c = kCharBase; c < kCharBase + kCols * kRows; ++c) {
        int idx = c - kCharBase;
        int col = idx % kCols;
        int row = idx / kCols;
        buf[0] = static_cast<char>(c);
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

    TextureManager::GetInstance()->LoadFromRawRGBA8(atlasKey, rgba.data(), kAtlasW, kAtlasH);
    // FlushUploads は SceneManager がシーン初期化後に一括で行う
}

// JP アトラス ひらがな・カタカナ・指定漢字を 16x16 グリッドで焼く

int FontRenderer::GetJpGlyphIdx(wchar_t c) const
{
    const auto& indexOf = GetJpGlyphTable().indexOf;
    auto it = indexOf.find(c);
    return it != indexOf.end() ? it->second : -1;
}

void FontRenderer::BuildJpAtlas(bool bold)
{
    const char* atlasKey = bold ? kJpAtlasKey : kJpAtlasKeyRegular;
    if (TextureManager::GetInstance()->HasTexture(atlasKey)) {
        return;
    }

    const auto& glyphs = GetJpGlyphTable().glyphs;
    int totalGlyphs = static_cast<int>(glyphs.size());
    int atlasRows = (totalGlyphs + kJpCols - 1) / kJpCols;
    (bold ? jpAtlasRows_ : jpAtlasRowsRegular_) = atlasRows;
    int atlasW = kJpCols * kJpCharW; // 256
    int atlasH = atlasRows * kJpCharH;

    BITMAPINFO bmi { };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = atlasW;
    bmi.bmiHeader.biHeight = -atlasH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP hBmp = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ENGINE_ASSERT(hBmp && bits);

    HDC hdcMem = CreateCompatibleDC(nullptr);
    HBITMAP hOld = static_cast<HBITMAP>(SelectObject(hdcMem, hBmp));
    memset(bits, 0, static_cast<size_t>(atlasW) * atlasH * 4);

    LOGFONTW lf { };
    lf.lfHeight = -(kJpCharH - 2);
    lf.lfWeight = bold ? FW_BOLD : FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfOutPrecision = OUT_TT_PRECIS;
    lf.lfQuality = ANTIALIASED_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    wcscpy_s(lf.lfFaceName, L"MS Gothic");

    HFONT hFont = CreateFontIndirectW(&lf);
    ENGINE_ASSERT(hFont);
    HFONT hOldFont = static_cast<HFONT>(SelectObject(hdcMem, hFont));

    SetBkMode(hdcMem, TRANSPARENT);
    SetTextColor(hdcMem, RGB(255, 255, 255));

    wchar_t wbuf[2] = { 0, 0 };
    auto renderAt = [&](int idx, wchar_t ch) {
        wbuf[0] = ch;
        int col = idx % kJpCols;
        int row = idx / kJpCols;
        TextOutW(hdcMem, col * kJpCharW, row * kJpCharH, wbuf, 1);
    };

    for (int i = 0; i < totalGlyphs; ++i) {
        renderAt(i, glyphs[i]);
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

    TextureManager::GetInstance()->LoadFromRawRGBA8(atlasKey, rgba.data(), atlasW, atlasH);
    // FlushUploads は SceneManager がシーン初期化後に一括で行う
}

void FontRenderer::Initialize(SpriteCommon* spriteCommon)
{
    spriteCommon_ = spriteCommon;
    BuildAtlas(true);
    BuildAtlas(false);
    BuildJpAtlas(true);
    BuildJpAtlas(false);

    // 上限分のGPUバッファを先に作らず、よく使う文字数だけ準備する。
    auto preparePool = [&](std::vector<Sprite>& pool, int limit, int initialCount, const char* atlas) {
        pool.clear();
        pool.reserve(limit);
        for (int i = 0; i < initialCount; ++i) {
            pool.emplace_back();
            pool.back().Initialize(spriteCommon_, atlas);
        }
    };
    preparePool(sprites_, kMaxChars, 128, kAtlasKey);
    preparePool(spritesRegular_, kMaxRegularChars, 32, kAtlasKeyRegular);
    preparePool(jpSprites_, kMaxChars, 128, kJpAtlasKey);
    preparePool(jpSpritesRegular_, kMaxRegularChars, 32, kJpAtlasKeyRegular);
}

Sprite& FontRenderer::AcquireGlyphSprite(std::vector<Sprite>& pool, int& index, const char* atlas)
{
    if (index >= static_cast<int>(pool.size())) {
        pool.emplace_back();
        pool.back().Initialize(spriteCommon_, atlas);
    }
    return pool[index++];
}
void FontRenderer::DrawString(const std::string& text, float x, float y,
    float scale, const Vector4& color, bool bold)
{
    cmds_.push_back({ text, x, y, scale, color, bold });
}

void FontRenderer::DrawStringW(const std::wstring& text, float x, float y,
    float scale, const Vector4& color, bool bold)
{
    cmdsW_.push_back({ text, x, y, scale, color, bold });
}

void FontRenderer::Reset()
{
    cmds_.clear();
    cmdsW_.clear();
    spriteIdx_ = 0;
    spriteRegularIdx_ = 0;
    jpSpriteIdx_ = 0;
    jpSpriteRegularIdx_ = 0;
}

void FontRenderer::Draw()
{
    // ── ASCII 文字列 ──────────────────────────────────────────────────
    for (const auto& cmd : cmds_) {
        auto& pool = cmd.bold ? sprites_ : spritesRegular_;
        auto& poolIdx = cmd.bold ? spriteIdx_ : spriteRegularIdx_;
        const int poolMax = cmd.bold ? kMaxChars : kMaxRegularChars;
        float cx = cmd.x;
        float cy = cmd.y;
        for (unsigned char c : cmd.text) {
            if (c == '\r') { continue; }
            if (c == '\n') {
                cx = cmd.x;
                cy += (kCharH + 4) * cmd.scale;
                continue;
            }
            if (poolIdx >= poolMax) {
                break;
            }
            int idx = static_cast<int>(c) - kCharBase;
            if (idx < 0 || idx >= kCols * kRows) {
                cx += kCharW * cmd.scale;
                continue;
            }
            int col = idx % kCols;
            int row = idx / kCols;
            auto& s = AcquireGlyphSprite(pool, poolIdx, cmd.bold ? kAtlasKey : kAtlasKeyRegular);
            s.SetPosition({ cx, cy });
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
        auto& pool = cmd.bold ? sprites_ : spritesRegular_;
        auto& poolIdx = cmd.bold ? spriteIdx_ : spriteRegularIdx_;
        const int poolMax = cmd.bold ? kMaxChars : kMaxRegularChars;
        auto& jpPool = cmd.bold ? jpSprites_ : jpSpritesRegular_;
        auto& jpPoolIdx = cmd.bold ? jpSpriteIdx_ : jpSpriteRegularIdx_;
        const int jpPoolMax = cmd.bold ? kMaxChars : kMaxRegularChars;
        float cx = cmd.x;
        float cy = cmd.y;
        for (wchar_t wc : cmd.text) {
            if (wc == L'\r') { continue; }
            if (wc == L'\n') {
                cx = cmd.x;
                cy += (kJpCharH + 4) * cmd.scale;
                continue;
            }
            if (wc < 128) {
                // ASCII 部分 → ASCII アトラス
                int idx = static_cast<int>(wc) - kCharBase;
                if (idx >= 0 && idx < kCols * kRows && poolIdx < poolMax) {
                    int col = idx % kCols;
                    int row = idx / kCols;
                    auto& s = AcquireGlyphSprite(pool, poolIdx, cmd.bold ? kAtlasKey : kAtlasKeyRegular);
                    s.SetPosition({ cx, cy });
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
                if (jpIdx >= 0 && jpPoolIdx < jpPoolMax) {
                    int col = jpIdx % kJpCols;
                    int row = jpIdx / kJpCols;
                    auto& s = AcquireGlyphSprite(jpPool, jpPoolIdx, cmd.bold ? kJpAtlasKey : kJpAtlasKeyRegular);
                    s.SetPosition({ cx, cy });
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
