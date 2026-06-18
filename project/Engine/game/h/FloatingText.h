/**
 * @file FloatingText.h
 * @brief フローティングテキスト（ダメージ数値・ポップアップ文字が浮かんで消えるエフェクト）
 *
 * 【概要】
 *   FontRenderer を使って文字列を画面上に表示し、時間経過で上昇しながらフェードアウトする。
 *   ダメージ表示、回復量、スコア加算、デバッグ情報など幅広く使える。
 *
 * 【使い方】
 *   // 初期化（一度だけ、FontRenderer の初期化後に）
 *   FloatingText::GetInstance()->Initialize(fontRenderer_);
 *
 *   // テキストをスポーン
 *   FloatingTextParams p;
 *   p.text      = std::to_string(damage);    // 表示する文字列
 *   p.x         = screenX;                    // 画面上の X 座標（ピクセル）
 *   p.y         = screenY;                    // 画面上の Y 座標（ピクセル）
 *   p.color     = { 1.0f, 0.3f, 0.3f, 1.0f }; // 赤色でダメージを表示
 *   p.scale     = 2.0f;                        // 文字サイズ倍率
 *   p.duration  = 0.8f;                        // 0.8 秒で消える
 *   p.riseSpeed = 40.0f;                       // 上昇速度（px/秒）
 *   p.fadeStart = 0.4f;                        // duration の 40% 経過後からフェード開始
 *   FloatingText::GetInstance()->Spawn(p);
 *
 *   // 毎フレーム（Update と Draw）
 *   FloatingText::GetInstance()->Update(dt);
 *   FloatingText::GetInstance()->Draw(); // FontRenderer::Draw() の前に呼ぶ
 *
 *   // シーン切り替え時に全テキストを削除
 *   FloatingText::GetInstance()->Clear();
 */
#pragma once
#include "MakeAffine.h"
#include "FontRenderer.h"
#include <string>
#include <vector>

/// @brief フローティングテキストの生成パラメータ
struct FloatingTextParams {
    std::string text;                                   ///< 表示する文字列
    float x         = 0.0f;                             ///< 初期 X 座標（ピクセル）
    float y         = 0.0f;                             ///< 初期 Y 座標（ピクセル）
    float scale     = 1.5f;                             ///< 文字サイズ倍率（FontRenderer::DrawString の scale）
    Vector4 color   = { 1.0f, 1.0f, 1.0f, 1.0f };     ///< 文字色 RGBA（アルファはフェードで上書きされる）
    float duration  = 0.7f;                             ///< 表示時間（秒）
    float riseSpeed = 30.0f;                            ///< 上昇速度（ピクセル / 秒）
    float fadeStart = 0.4f;                             ///< フェードアウト開始タイミング（duration に対する比率）
};

class FloatingText {
public:
    /// @brief シングルトンインスタンスを取得する
    static FloatingText* GetInstance();

    /**
     * @brief 初期化。FontRenderer のポインタを受け取る
     * @param fontRenderer FontRenderer クラスのポインタ（シーンが持つ fontRenderer_ など）
     */
    void Initialize(FontRenderer* fontRenderer);

    /**
     * @brief 新しいフローティングテキストを登録する
     * @param params 表示パラメータ（FloatingTextParams 参照）
     */
    void Spawn(const FloatingTextParams& params);

    /**
     * @brief 毎フレーム位置とタイマーを更新し、時間切れのテキストを削除する
     * @param dt デルタタイム（秒）
     */
    void Update(float dt);

    /**
     * @brief 全テキストを FontRenderer に描画コマンドとして積む
     * @note FontRenderer::Draw() の前に呼ぶこと
     */
    void Draw();

    /// @brief 全テキストを即座に削除する（シーン切り替え時などに呼ぶ）
    void Clear();

private:
    FloatingText() = default;

    /// @brief 内部管理用エントリ（生成時のパラメータ + 現在の状態）
    struct Entry {
        FloatingTextParams params; ///< 生成時に渡されたパラメータ
        float timer = 0.0f;       ///< 経過時間（秒）
        float x     = 0.0f;      ///< 現在の X 座標（フレームごとに更新）
        float y     = 0.0f;      ///< 現在の Y 座標（上昇アニメーション中）
    };

    FontRenderer*      fontRenderer_ = nullptr; ///< 文字描画クラスのポインタ
    std::vector<Entry> entries_;                ///< アクティブなフローティングテキストのリスト
};
