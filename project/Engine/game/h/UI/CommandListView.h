/**
 * @file CommandListView.h
 * @brief 持っている武器ごとの技と入力を一覧で見せるコマンド表の画面部品を定義するファイル
 */
#pragma once
#include "Vector2.h"
#include "Vector3.h"
#include <d3d12.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace engine {
class Input;
class Audio;
class DirectXCommon;
} // namespace engine

namespace engine::graphics {
class Camera;
class ModelCommon;
class Object3d;
class Object3dCommon;
class ShadowManager;
class SrvManager;
} // namespace engine::graphics

namespace engine::game {

class FontRenderer;
class Player;
struct MeleeAttackDef;
enum class WeaponType;

/** @brief コマンド表の1行（技1つぶん） */
struct CommandMove {
    std::wstring category; ///< 区分（地上・空中・打ち上げ・固有技など）
    std::wstring name; ///< 技名
    std::wstring input; ///< 入力（{Attack} などの操作名は表示の時にボタン名へ置き換える）
    std::wstring detail; ///< 下の説明欄に出す詳しい説明
    std::vector<std::string> preview; ///< 見本で順に再生する段のID（skill は固有技の連撃をまるごと）
};

/** @brief 1武器ぶんのコマンド表 */
struct CommandSheet {
    std::wstring tagline; ///< 武器の一言説明
    std::vector<CommandMove> moves; ///< その武器だけの技
};

/** @brief 技の見本を3Dで描くために、シーンが持つ描画の道具を借りる */
struct CommandPreviewRenderer {
    engine::DirectXCommon* dxCommon = nullptr; ///< コマンドリストと深度の取得先
    engine::graphics::ModelCommon* modelCommon = nullptr; ///< モデル描画のパイプライン
    engine::graphics::Object3dCommon* objectCommon = nullptr; ///< ライト
    engine::graphics::ShadowManager* shadowManager = nullptr; ///< 影
    engine::graphics::SrvManager* srvManager = nullptr; ///< 影のテクスチャの束縛に使う
    D3D12_CPU_DESCRIPTOR_HANDLE rtv { }; ///< 文字を描いた描画先（ポストエフェクトの描画先かバックバッファ）
};

/**
 * @brief コマンド表（Resources/Config/command_list.json）
 * @note 武器ごとの技に、全武器共通の操作（銃・回避・覚醒など）を後ろへ足して並べる。
 *       上で武器を切り替え、真ん中の一覧で技を選ぶと、下の欄にその技の説明が、右の欄にその技の見本が出る。
 *       まだ手に入れていない武器は名前も技も伏せる。
 */
class CommandListView {
public:
    CommandListView();
    ~CommandListView();

    /**
     * @brief 文字の描画先と効果音の再生先を受け取り、JSON を読み込む
     * @param fontRenderer 文字とパネルを積む先（描画の実行は呼び出し側の Draw() で行う）
     * @param audio        カーソル移動の効果音（nullptrなら鳴らさない）
     */
    void Initialize(FontRenderer* fontRenderer, engine::Audio* audio);

    /**
     * @brief 見本で技を受ける的のモデルを読み込む（シーンの初期化で1回。呼ばなければ的は出さない）
     * @param modelCommon 的のモデルの描画に使う（シーンが持つもの）
     */
    void InitializePreview(engine::graphics::ModelCommon* modelCommon);

    /**
     * @brief 見本を演じるプレイヤーを渡す（渡さなければ見本は出さない）
     * @param actor 見本の姿勢を取らせるプレイヤー（非所有。開いている間はゲームの更新を止めておくこと）
     */
    void SetPreviewActor(Player* actor) {
        previewActor_ = actor;
    }

    /** @brief 開いた時に呼ぶ（今装備している武器のページから始める） */
    void Open();

    /** @brief 閉じた時に呼ぶ（見本の姿勢をやめて元に戻す。戻る操作で閉じた時は Update() の中で呼ばれる） */
    void Close();

    /**
     * @brief 武器の切り替え・技の選択を受け付け、見本を1フレーム進める
     * @return 戻る操作がされたらtrue
     */
    bool Update(engine::Input* input);

    /** @brief 文字とパネルを積む（毎フレーム、FontRenderer::Reset() の後に呼ぶ） */
    void Draw(const engine::Input* input);

    /**
     * @brief 見本の欄にプレイヤーを3Dで描く（FontRenderer::Draw() の後に呼ぶ。見本が無ければ何もしない）
     * @note 描画先とビューポートは渡された rtv の全画面に戻して終わる。パイプラインはモデル用のまま
     */
    void DrawPreview(const CommandPreviewRenderer& renderer);

private:
    /** @brief 今のページに並べる技（武器の技＋共通の操作）を作り直す */
    void RebuildRows();
    /** @brief 選んでいる技の見本で再生する段を選び直し、最初から再生する */
    void RebuildPreview();
    /** @brief 見本を1フレーム進め、見本の姿勢をプレイヤーに取らせる */
    void AdvancePreview();
    /** @brief 見本の段の時間を進める（ヒットストップ中は止め、段を出し切ったら次の段へ、最後まで出したら最初から） */
    void AdvancePreviewClock();
    /** @brief 段の当たる時刻に的を吹き飛ばし、的の動きを1フレーム進める */
    void UpdatePreviewTarget(const MeleeAttackDef& step, float knockbackMult);
    /** @brief 的とプレイヤーの位置を最初に戻す（最初の段からやり直す時） */
    void ResetPreviewScene();
    /** @brief 指定方向へ、持っている武器の中で次のページへ移る（持っていない武器も伏せて並べる） */
    void MoveWeapon(int step);
    /** @brief 説明文を指定幅で折り返す */
    static std::wstring WrapText(const std::wstring& text, float maxWidth, float scale);

    FontRenderer* fontRenderer_ = nullptr;
    engine::Audio* audio_ = nullptr;

    std::map<WeaponType, CommandSheet> sheets_; ///< 武器ごとの技
    std::vector<CommandMove> commonMoves_; ///< 全武器共通の操作
    std::vector<const CommandMove*> rows_; ///< 今のページに並んでいる技
    int weaponIndex_ = 0; ///< WeaponManager::GetList() の添字
    int cursor_ = 0; ///< 選んでいる行
    int scroll_ = 0; ///< 一覧の先頭に出している行

    /** @brief 見本の見せ方の調整値（command_list.json の preview_camera / preview_target で上書きできる） */
    struct PreviewTuning {
        float cameraDistance; ///< カメラからプレイヤーと的の真ん中までの奥行き
        float cameraHeight; ///< プレイヤーの中心から見たカメラの高さ
        float cameraPitch; ///< カメラの見下ろし角（ラジアン）
        float cameraFovY; ///< カメラの縦の画角（ラジアン）
        float targetDistance; ///< 的を置く、プレイヤーの正面への距離
        float targetMinGap; ///< 踏み込んでも的とこれ以上は近づかない
        float targetMaxDistance; ///< 吹き飛んでもプレイヤーからこれ以上は離れない（欄の外へ出さない）
        float targetGravity; ///< 的に掛かる重力（毎フレームの速度の減り）
        float targetDragX; ///< 的の横の速度の毎フレームの減衰率
        float targetDragY; ///< 的の縦の速度の毎フレームの減衰率
        float airHeight; ///< 空中の段を見せる時に、プレイヤーと的を持ち上げる高さ
    };

    Player* previewActor_ = nullptr; ///< 見本を演じるプレイヤー（非所有）
    std::unique_ptr<engine::graphics::Camera> previewCamera_; ///< 見本の欄だけを写すカメラ
    std::unique_ptr<engine::graphics::Object3d> previewTarget_; ///< 見本で技を受ける的（InitializePreview() で作る）
    std::vector<const MeleeAttackDef*> previewSteps_; ///< 見本で順に再生する段
    std::vector<bool> previewAirSteps_; ///< 段ごとに空中の段か（持ち上げて見せる）
    int previewStep_ = 0; ///< 再生中の段の番号
    float previewTime_ = 0.0f; ///< 再生中の段の経過秒
    float previewHitStop_ = 0.0f; ///< 当たった時に見本を止めておく残り秒数
    float previewLunge_ = 0.0f; ///< 出し終えた段の踏み込みの合計
    bool previewHitDone_ = false; ///< 再生中の段で的を吹き飛ばしたか
    bool previewPosed_ = false; ///< プレイヤーに見本の姿勢を取らせているか（やめる時に元へ戻す）
    engine::Vector3 targetOffset_ { }; ///< 的の位置（プレイヤーの本当の位置から、正面を+Xとした向きで）
    engine::Vector3 targetVelocity_ { }; ///< 的の速度（同じ向き）
    float targetFlash_ = 0.0f; ///< 的を光らせておく残り秒数
    PreviewTuning tuning_ { };
    engine::Vector2 previewPos_ { }; ///< 見本の欄の左上（Draw() で決め、DrawPreview() が同じ欄へ描く）
    engine::Vector2 previewSize_ { }; ///< 見本の欄の大きさ
};

} // namespace engine::game
