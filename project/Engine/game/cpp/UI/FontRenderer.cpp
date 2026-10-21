/**
 * @file FontRenderer.cpp
 * @brief ビットマップフォントアトラス生成とASCII/日本語文字列描画（FontRenderer）の実装
 */
#include "FontRenderer.h"
#include "TextBufferCapacity.h"
#include "Logger.h"
#include "StringUtility.h"
#include "TextureManager.h"
#define NOMINMAX
#include "EngineAssert.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <windows.h>
using namespace engine::graphics;

namespace engine::game {
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
                                  L"所済次開始" // タイトル・結果表示
                                  L"切替散" // TrainingScene追加
                                  L"満打上空手戻重騎死狂突掬剛叩落剣下刈魂斧" // 武器コマンド・スタイル名・マップ説明の不足分
                                  L"締踏込貫通" // 銃コンボのコマンド説明
                                  L"了交体備入前区口合固壁壊変外奪完左技接換攻有杯棄槍画直瞬破練装解訓赤迅間障青"
                                  L"寄押" // ロックオンUI（最寄り・長押し）
                                  L"収避無身可割価待構評吸" // 回避/無敵・武器吸収・マップ評価UIの不足分
                                  L"空中追撃段別推奨順番" // コンボルートガイド
                                  L"擲" // レベル2の案内文（JIS第二水準のため範囲外）
                                  L"威混" // 武器の疲労警告
                                  // 以下はShift_JIS範囲にも含まれるが、範囲の一括追加を外しても画面の文章が欠けないよう明示しておく
                                  L"→○●　、。々（）：？" // HUD・マップ・案内文の記号
                                  L"一予二先光内円再出制削告呼囲型基場実寸屋崩帰常広床役径後必快扉投抜探揺文時来桁案機正殺気波消渡渦準"
                                  L"溜滅滞演灰特牽生目着短秒程立第箱範約紙索緑能色荒見読起超足車転軽輪近退逆遅違遠還部量面駐"; // 武器説明・レベル案内文・グラフの文章
static constexpr uint32_t kHiraganaStart = 0x3041;
static constexpr uint32_t kHiraganaEnd = 0x3096;
static constexpr uint32_t kKatakanaStart = 0x30A0;
static constexpr uint32_t kKatakanaEnd = 0x30FF;
static constexpr int kHiraganaCount = static_cast<int>(kHiraganaEnd - kHiraganaStart + 1); // 86
static constexpr int kKatakanaCount = static_cast<int>(kKatakanaEnd - kKatakanaStart + 1); // 96

// Shift_JIS(CP932) の全角記号・英数字・JIS第一/第二水準漢字・NEC/IBM拡張文字をまとめて焼き込み、
// レベルデータ等で自由に書かれた文章でも文字が欠けないようにする（外字領域0xF040-0xF9FCだけは除く）
static constexpr UINT kShiftJisCodePage = 932;
static constexpr int kSjisLeadFirst = 0x81;
static constexpr int kSjisLeadLowerLast = 0x9F; // 第一水準・第二水準前半
static constexpr int kSjisLeadUpperFirst = 0xE0; // 第二水準後半
static constexpr int kSjisLeadUpperLast = 0xFC; // IBM拡張文字まで
static constexpr int kSjisUserDefinedFirst = 0xF0; // 外字領域（文字が割り当てられていない）
static constexpr int kSjisUserDefinedLast = 0xF9;
static constexpr int kSjisTrailFirst = 0x40;
static constexpr int kSjisTrailLast = 0xFC;
static constexpr int kSjisTrailInvalid = 0x7F;
static constexpr uint32_t kAsciiLimit = 128;
static constexpr int kLineGap = 4; // 改行時に行の間へ空ける隙間（文字の高さに足すpx、スケール前）
static constexpr const char* kPanelTexture = "Resources/white.png";

// 見た目は同じでも文字コードが違う記号（入力環境やコピー元によって混ざる）。CP932経由では作られないので明示的に足す
static const wchar_t kLookalikeSymbols[] = L"〜−‖¢£¬—–―‥…·";

namespace {
constexpr WORD kDibBitsPerPixel = 32; // 描画先の DIB は 1 画素 32bit（BGRA）
constexpr size_t kBytesPerPixel = 4; // BGRA / RGBA の 1 画素のバイト数
// GDI の DIB の 1 画素の並び（BGRA）
constexpr size_t kDibBlue = 0;
constexpr size_t kDibGreen = 1;
constexpr size_t kDibRed = 2;
// テクスチャの 1 画素の並び（RGBA）
constexpr size_t kRgbaRed = 0;
constexpr size_t kRgbaGreen = 1;
constexpr size_t kRgbaBlue = 2;
constexpr size_t kRgbaAlpha = 3;
constexpr int kColorChannelCount = 3; // 輝度を求める時に平均する色の数（R・G・B）
constexpr uint8_t kChannelMax = 255; // 1 チャンネルの最大値
constexpr int kAsciiFontHeight = -13; // ASCII のフォントの高さ（負の値は文字の高さの指定、px）
constexpr int kAsciiRowOffset = 1; // ASCII の文字をマスの上端から下げる量（px）
constexpr int kJpFontCellMargin = 2; // JP のフォントをマスより小さくする量（px）
constexpr int kInitialBoldSprites = 128; // 太字の文字スプライトを最初に用意する数
constexpr int kInitialRegularSprites = 32; // 細字の文字スプライトを最初に用意する数

/** @brief GDI で文字を描く先（DIBSection とメモリ DC） */
struct GdiCanvas {
    HBITMAP bitmap = nullptr;
    HBITMAP previousBitmap = nullptr;
    HDC dc = nullptr;
    void* bits = nullptr;
};

/** @brief 黒で塗った width×height の 32bit DIB を作り、白い文字を描ける状態にする */
GdiCanvas BeginGdiCanvas(int width, int height)
{
    BITMAPINFO bmi { };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // 負の高さで上から下の並びにする
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = kDibBitsPerPixel;
    bmi.bmiHeader.biCompression = BI_RGB;

    GdiCanvas canvas;
    canvas.bitmap = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &canvas.bits, nullptr, 0);
    ENGINE_ASSERT(canvas.bitmap && canvas.bits);
    canvas.dc = CreateCompatibleDC(nullptr);
    canvas.previousBitmap = static_cast<HBITMAP>(SelectObject(canvas.dc, canvas.bitmap));

    // 背景を黒で塗りつぶし、文字は白で背景を残して描く
    memset(canvas.bits, 0, static_cast<size_t>(width) * height * kBytesPerPixel);
    SetBkMode(canvas.dc, TRANSPARENT);
    SetTextColor(canvas.dc, RGB(kChannelMax, kChannelMax, kChannelMax));
    return canvas;
}

/** @brief 描き終えた DIB を片付け、白い文字の輝度をアルファにした RGBA の画素列を返す */
std::vector<uint8_t> FinishGdiCanvas(GdiCanvas& canvas, int width, int height)
{
    GdiFlush();
    SelectObject(canvas.dc, canvas.previousBitmap);
    DeleteDC(canvas.dc);

    // BGRA（GDI DIB）→ RGBA（DX12用）。白テキスト on 黒背景なので輝度をアルファに使う
    const auto* src = static_cast<const uint8_t*>(canvas.bits);
    const size_t pixelCount = static_cast<size_t>(width) * height;
    std::vector<uint8_t> rgba(pixelCount * kBytesPerPixel);
    for (size_t i = 0; i < pixelCount; ++i)
    {
        const uint8_t* pixel = src + i * kBytesPerPixel;
        uint8_t* out = rgba.data() + i * kBytesPerPixel;
        const int luminance = (static_cast<int>(pixel[kDibRed]) + pixel[kDibGreen] + pixel[kDibBlue]) / kColorChannelCount;
        out[kRgbaRed] = out[kRgbaGreen] = out[kRgbaBlue] = kChannelMax;
        out[kRgbaAlpha] = static_cast<uint8_t>(luminance);
    }
    DeleteObject(canvas.bitmap);
    return rgba;
}

/** @brief JPアトラスに焼く文字の並び（かな→Shift_JIS範囲→kJpExtraの残り）と文字→グリフ番号の対応表 */
struct JpGlyphTable {
    std::vector<wchar_t> glyphs;
    std::unordered_map<wchar_t, int> indexOf;

    /** @brief 文字をアトラスに焼く一覧へ追加する（ASCIIと登録済みの文字は飛ばす） */
    void Add(wchar_t ch)
    {
        if (static_cast<uint32_t>(ch) < kAsciiLimit || indexOf.count(ch))
        {
            return;
        }
        indexOf.emplace(ch, static_cast<int>(glyphs.size()));
        glyphs.push_back(ch);
    }
};

/** @brief アトラスに無い文字を、文字ごとに1回だけログへ出す（黙って抜けると気づけないため） */
void ReportMissingGlyph(wchar_t ch, const std::wstring& text)
{
    static std::unordered_set<wchar_t> reported;
    if (!reported.insert(ch).second)
    {
        return;
    }
    char code[TextBufferCapacity::kCodepoint];
    std::snprintf(code, sizeof(code), "U+%04X", static_cast<unsigned>(ch));
    Logger::LogWarning(std::string("[FontRenderer] フォントに無い文字を描画できませんでした ") + code
        + " : " + StringUtility::ConvertString(text) + "  (FontRenderer.cppのkJpExtraへ追加してください)");
}

/** @brief 1フレームの文字数上限に達したことを、1回だけログへ出す */
void ReportGlyphPoolFull(int limit)
{
    static bool reported = false;
    if (reported)
    {
        return;
    }
    reported = true;
    Logger::LogWarning("[FontRenderer] 1フレームの文字数上限(" + std::to_string(limit)
        + ")に達したため、以降の文字を描画できませんでした。FontRenderer.hのkMaxChars/kMaxRegularCharsを上げてください");
}

/** @brief 日本語のアトラスに焼く文字の表を返す（初回に作る） */
const JpGlyphTable& GetJpGlyphTable()
{
    static const JpGlyphTable table = [] {
        JpGlyphTable t;
        // ひらがな・カタカナは範囲ごと登録する
        for (int i = 0; i < kHiraganaCount; ++i)
        {
            t.Add(static_cast<wchar_t>(kHiraganaStart + i));
        }
        for (int i = 0; i < kKatakanaCount; ++i)
        {
            t.Add(static_cast<wchar_t>(kKatakanaStart + i));
        }
        // Shift_JIS で表せる2バイト文字をすべて試し、変換できた文字を登録する（機種依存の領域は除く）
        for (int lead = kSjisLeadFirst; lead <= kSjisLeadUpperLast; ++lead)
        {
            const bool validLead = lead <= kSjisLeadLowerLast
                || (lead >= kSjisLeadUpperFirst && (lead < kSjisUserDefinedFirst || lead > kSjisUserDefinedLast));
            if (!validLead)
            {
                continue;
            }
            for (int trail = kSjisTrailFirst; trail <= kSjisTrailLast; ++trail)
            {
                if (trail == kSjisTrailInvalid)
                {
                    continue;
                }
                const char bytes[2] = { static_cast<char>(lead), static_cast<char>(trail) };
                wchar_t wc = 0;
                if (MultiByteToWideChar(kShiftJisCodePage, MB_ERR_INVALID_CHARS, bytes, 2, &wc, 1) == 1)
                {
                    t.Add(wc);
                }
            }
        }
        // Shift_JIS の範囲外でゲームに使う文字と、見た目の似た記号を足す
        for (int i = 0; kJpExtra[i]; ++i)
        {
            t.Add(kJpExtra[i]);
        }
        for (int i = 0; kLookalikeSymbols[i]; ++i)
        {
            t.Add(kLookalikeSymbols[i]);
        }
        return t;
    }();
    return table;
}
} // namespace

/** @brief GDIでCourier NewのASCII文字をDIBSectionへ描画し、ASCIIアトラステクスチャとして登録する（登録済みなら何もしない） */
void FontRenderer::BuildAtlas(bool bold)
{
    const char* atlasKey = bold ? kAtlasKey : kAtlasKeyRegular;
    if (TextureManager::GetInstance()->HasTexture(atlasKey))
    {
        return;
    }

    // GDI で Courier New を 32bit DIBSection に描画する
    GdiCanvas canvas = BeginGdiCanvas(kAtlasW, kAtlasH);

    // フォント  Courier New（負の高さは文字の高さの指定）
    LOGFONTA lf { };
    lf.lfHeight = kAsciiFontHeight;
    lf.lfWeight = bold ? FW_BOLD : FW_NORMAL;
    lf.lfCharSet = ANSI_CHARSET;
    lf.lfOutPrecision = OUT_TT_PRECIS;
    lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lf.lfQuality = ANTIALIASED_QUALITY;
    lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
    strcpy_s(lf.lfFaceName, "Courier New");

    HFONT hFont = CreateFontIndirectA(&lf);
    ENGINE_ASSERT(hFont);
    HFONT hOldFont = static_cast<HFONT>(SelectObject(canvas.dc, hFont));

    // ASCII 32〜127 を atlas に描く
    char buf[TextBufferCapacity::kCharacter] = { 0, 0 };
    for (int c = kCharBase; c < kCharBase + kCols * kRows; ++c)
    {
        int idx = c - kCharBase;
        int col = idx % kCols;
        int row = idx / kCols;
        buf[0] = static_cast<char>(c);
        TextOutA(canvas.dc, col * kCharW, row * kCharH + kAsciiRowOffset, buf, 1);
    }

    SelectObject(canvas.dc, hOldFont);
    DeleteObject(hFont);

    // 白い文字の輝度をアルファにしたテクスチャとして登録する
    const std::vector<uint8_t> rgba = FinishGdiCanvas(canvas, kAtlasW, kAtlasH);
    TextureManager::GetInstance()->LoadFromRawRGBA8(atlasKey, rgba.data(), kAtlasW, kAtlasH);
    // FlushUploads は SceneManager がシーン初期化後に一括で行う
}

// JP アトラス ひらがな・カタカナ・指定漢字を 16x16 グリッドで焼く

/** @brief 文字の JP アトラス上のグリフ番号を返す（焼いていない文字は -1） */
int FontRenderer::GetJpGlyphIdx(wchar_t c) const
{
    const auto& indexOf = GetJpGlyphTable().indexOf;
    auto it = indexOf.find(c);
    return it != indexOf.end() ? it->second : -1;
}

/** @brief ひらがな・カタカナ・追加漢字グリフをGDIで描画し、JPアトラステクスチャとして登録する（登録済みなら何もしない） */
void FontRenderer::BuildJpAtlas(bool bold)
{
    const char* atlasKey = bold ? kJpAtlasKey : kJpAtlasKeyRegular;
    if (TextureManager::GetInstance()->HasTexture(atlasKey))
    {
        return;
    }

    // 焼く文字の数から、アトラスの行数と大きさを決める
    const auto& glyphs = GetJpGlyphTable().glyphs;
    int totalGlyphs = static_cast<int>(glyphs.size());
    int atlasRows = (totalGlyphs + kJpCols - 1) / kJpCols;
    (bold ? jpAtlasRows_ : jpAtlasRowsRegular_) = atlasRows;
    int atlasW = kJpCols * kJpCharW;
    int atlasH = atlasRows * kJpCharH;

    GdiCanvas canvas = BeginGdiCanvas(atlasW, atlasH);

    // フォント  MS Gothic（マスの上下に少し余白を残す高さ）
    LOGFONTW lf { };
    lf.lfHeight = -(kJpCharH - kJpFontCellMargin);
    lf.lfWeight = bold ? FW_BOLD : FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfOutPrecision = OUT_TT_PRECIS;
    lf.lfQuality = ANTIALIASED_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    wcscpy_s(lf.lfFaceName, L"MS Gothic");

    HFONT hFont = CreateFontIndirectW(&lf);
    ENGINE_ASSERT(hFont);
    HFONT hOldFont = static_cast<HFONT>(SelectObject(canvas.dc, hFont));

    // グリフ番号の順にグリッドへ1文字ずつ描く
    wchar_t wbuf[TextBufferCapacity::kCharacter] = { 0, 0 };
    for (int i = 0; i < totalGlyphs; ++i)
    {
        wbuf[0] = glyphs[i];
        int col = i % kJpCols;
        int row = i / kJpCols;
        TextOutW(canvas.dc, col * kJpCharW, row * kJpCharH, wbuf, 1);
    }

    SelectObject(canvas.dc, hOldFont);
    DeleteObject(hFont);

    // 白い文字の輝度をアルファにしたテクスチャとして登録する
    const std::vector<uint8_t> rgba = FinishGdiCanvas(canvas, atlasW, atlasH);
    TextureManager::GetInstance()->LoadFromRawRGBA8(atlasKey, rgba.data(), atlasW, atlasH);
    // FlushUploads は SceneManager がシーン初期化後に一括で行う
}

/** @brief ASCII/JP各2種(Bold/Regular)のアトラスを構築（未生成なら）し、描画用スプライトプールを確保する */
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
        for (int i = 0; i < initialCount; ++i)
        {
            pool.emplace_back();
            pool.back().Initialize(spriteCommon_, atlas);
        }
    };
    preparePool(sprites_, kMaxChars, kInitialBoldSprites, kAtlasKey);
    preparePool(spritesRegular_, kMaxRegularChars, kInitialRegularSprites, kAtlasKeyRegular);
    preparePool(jpSprites_, kMaxChars, kInitialBoldSprites, kJpAtlasKey);
    preparePool(jpSpritesRegular_, kMaxRegularChars, kInitialRegularSprites, kJpAtlasKeyRegular);
}

/** @brief 必要な文字数に応じてスプライトを追加し、既存のものは再利用する */
Sprite& FontRenderer::AcquireGlyphSprite(std::vector<Sprite>& pool, int& index, const char* atlas)
{
    if (index >= static_cast<int>(pool.size()))
    {
        pool.emplace_back();
        pool.back().Initialize(spriteCommon_, atlas);
    }
    return pool[index++];
}
/** @brief アトラスのグリッドの glyphIndex 番目のマス（cellW×cellH、cols 列）を、画面の (x, y) へ描く */
void FontRenderer::DrawGlyph(std::vector<Sprite>& pool, int& poolIndex, const char* atlas, int glyphIndex,
    int cols, int cellW, int cellH, float x, float y, float scale, const Vector4& color)
{
    const int col = glyphIndex % cols;
    const int row = glyphIndex / cols;
    auto& s = AcquireGlyphSprite(pool, poolIndex, atlas);
    s.SetPosition({ x, y });
    s.SetSize({ static_cast<float>(cellW) * scale, static_cast<float>(cellH) * scale });
    s.SetTextureLeftTop({ static_cast<float>(col * cellW), static_cast<float>(row * cellH) });
    s.SetTextureSize({ static_cast<float>(cellW), static_cast<float>(cellH) });
    s.SetColor(color);
    s.Update();
    s.Draw();
}

/** @brief UTF-8 の文字列を描画予約する */
void FontRenderer::DrawString(const std::string& text, float x, float y,
    float scale, const Vector4& color, bool bold)
{
    // 半角用の経路は日本語(UTF-8の多バイト文字)を描けないため、混ざっていればワイド文字列の経路へ回す
    const bool hasMultiByte = std::any_of(text.begin(), text.end(),
        [](char c) {
            return static_cast<unsigned char>(c) >= kAsciiLimit;
        });
    if (hasMultiByte)
    {
        DrawStringW(StringUtility::ConvertString(text), x, y, scale, color, bold);
        return;
    }
    cmds_.push_back({ text, x, y, scale, color, bold });
}

/** @brief ワイド文字列を描画予約する */
void FontRenderer::DrawStringW(const std::wstring& text, float x, float y,
    float scale, const Vector4& color, bool bold)
{
    cmdsW_.push_back({ text, x, y, scale, color, bold });
}

/** @brief 文字の下に敷く単色の板を積む（Draw()で文字より先に描く） */
void FontRenderer::DrawPanel(float x, float y, float width, float height, const Vector4& color)
{
    panelCmds_.push_back({ x, y, width, height, color });
}

/** @brief DrawStringWで描いた時の大きさを返す（改行は行数として数える） */
Vector2 FontRenderer::MeasureStringW(const std::wstring& text, float scale)
{
    float lineWidth = 0.0f;
    float maxWidth = 0.0f;
    int lines = 1;
    for (wchar_t wc : text)
    {
        if (wc == L'\r')
        {
            continue;
        }
        if (wc == L'\n')
        {
            maxWidth = (std::max)(maxWidth, lineWidth);
            lineWidth = 0.0f;
            ++lines;
            continue;
        }
        lineWidth += (wc < kAsciiLimit ? kCharW : kJpCharW) * scale;
    }
    maxWidth = (std::max)(maxWidth, lineWidth);
    const float height = (kJpCharH * lines + kLineGap * (lines - 1)) * scale;
    return { maxWidth, height };
}

/** @brief 積んだ描画コマンドとスプライト使用数をクリアする（毎フレーム、DrawString系を呼ぶ前に呼ぶ） */
void FontRenderer::Reset()
{
    panelCmds_.clear();
    panelSpriteIdx_ = 0;
    cmds_.clear();
    cmdsW_.clear();
    spriteIdx_ = 0;
    spriteRegularIdx_ = 0;
    jpSpriteIdx_ = 0;
    jpSpriteRegularIdx_ = 0;
}

/** @brief Reset()以降に積んだASCII/JP文字列コマンドをすべてスプライトとして描画する */
void FontRenderer::Draw()
{
    // ── 下敷きの板（文字より先に描いて後ろへ回す） ───────────────────────
    for (const auto& panel : panelCmds_)
    {
        auto& s = AcquireGlyphSprite(panelSprites_, panelSpriteIdx_, kPanelTexture);
        s.SetPosition({ panel.x, panel.y });
        s.SetSize({ panel.width, panel.height });
        s.SetColor(panel.color);
        s.Update();
        s.Draw();
    }

    // ── ASCII 文字列 ──────────────────────────────────────────────────
    for (const auto& cmd : cmds_)
    {
        // 太字か通常かで使うスプライトの置き場を切り替える
        auto& pool = cmd.bold ? sprites_ : spritesRegular_;
        auto& poolIdx = cmd.bold ? spriteIdx_ : spriteRegularIdx_;
        const int poolMax = cmd.bold ? kMaxChars : kMaxRegularChars;
        float cx = cmd.x;
        float cy = cmd.y;
        // 1文字ずつ並べる（改行で行頭へ戻り、アトラスに無い文字は空白として進める）
        for (unsigned char c : cmd.text)
        {
            if (c == '\r')
            {
                continue;
            }
            if (c == '\n')
            {
                cx = cmd.x;
                cy += (kCharH + kLineGap) * cmd.scale;
                continue;
            }
            if (poolIdx >= poolMax)
            {
                ReportGlyphPoolFull(poolMax);
                break;
            }
            int idx = static_cast<int>(c) - kCharBase;
            if (idx < 0 || idx >= kCols * kRows)
            {
                cx += kCharW * cmd.scale;
                continue;
            }
            DrawGlyph(pool, poolIdx, cmd.bold ? kAtlasKey : kAtlasKeyRegular, idx,
                kCols, kCharW, kCharH, cx, cy, cmd.scale, cmd.color);
            cx += kCharW * cmd.scale;
        }
    }

    // ── 日本語（ASCII 混在可）ワイド文字列 ────────────────────────────
    for (const auto& cmd : cmdsW_)
    {
        // ASCII と日本語でアトラスと置き場を分ける
        auto& pool = cmd.bold ? sprites_ : spritesRegular_;
        auto& poolIdx = cmd.bold ? spriteIdx_ : spriteRegularIdx_;
        const int poolMax = cmd.bold ? kMaxChars : kMaxRegularChars;
        auto& jpPool = cmd.bold ? jpSprites_ : jpSpritesRegular_;
        auto& jpPoolIdx = cmd.bold ? jpSpriteIdx_ : jpSpriteRegularIdx_;
        const int jpPoolMax = cmd.bold ? kMaxChars : kMaxRegularChars;
        float cx = cmd.x;
        float cy = cmd.y;
        for (wchar_t wc : cmd.text)
        {
            if (wc == L'\r')
            {
                continue;
            }
            if (wc == L'\n')
            {
                cx = cmd.x;
                cy += (kJpCharH + kLineGap) * cmd.scale;
                continue;
            }
            if (wc < kAsciiLimit)
            {
                // ASCII 部分 → ASCII アトラス
                int idx = static_cast<int>(wc) - kCharBase;
                if (poolIdx >= poolMax)
                {
                    ReportGlyphPoolFull(poolMax);
                }
                if (idx >= 0 && idx < kCols * kRows && poolIdx < poolMax)
                {
                    DrawGlyph(pool, poolIdx, cmd.bold ? kAtlasKey : kAtlasKeyRegular, idx,
                        kCols, kCharW, kCharH, cx, cy, cmd.scale, cmd.color);
                }
                cx += kCharW * cmd.scale;
            }
            else
            {
                // 日本語 → JP アトラス
                int jpIdx = GetJpGlyphIdx(wc);
                if (jpIdx < 0)
                {
                    ReportMissingGlyph(wc, cmd.text);
                }
                else if (jpPoolIdx >= jpPoolMax)
                {
                    ReportGlyphPoolFull(jpPoolMax);
                }
                if (jpIdx >= 0 && jpPoolIdx < jpPoolMax)
                {
                    DrawGlyph(jpPool, jpPoolIdx, cmd.bold ? kJpAtlasKey : kJpAtlasKeyRegular, jpIdx,
                        kJpCols, kJpCharW, kJpCharH, cx, cy, cmd.scale, cmd.color);
                }
                cx += kJpCharW * cmd.scale;
            }
        }
    }
}

} // namespace engine::game
