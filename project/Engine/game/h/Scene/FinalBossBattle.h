/**
 * @file FinalBossBattle.h
 * @brief ラスボス戦（3面クリアの後に続く円形闘技場の戦い）の進行・ボスの技・闘技場のカメラ
 * @note プレイヤーとボスは横スクロールと同じ X 軸の上（z=0）で動かし、既存の戦闘の判定をそのまま使う。
 *       横への回り込みは、闘技場（床・柱・空）の方をボスやプレイヤーの周りに回して見せる
 */
#pragma once
#include "CollisionConfig.h"
#include "FinalBossArena.h"
#include "FinalBossHud.h"
#include "FinalBossTelegraph.h"
#include "Object3d.h"
#include "Vector3.h"
#include "Vector4.h"
#include "WeaponManager.h"
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace engine {
class Input;
}
namespace engine::graphics {
class Camera;
class ModelCommon;
class ParticleManager;
class SpriteCommon;
}

namespace engine::game {
class CameraShaker;
class EnemyEntity;
class FontRenderer;
class Player;
struct MeleeAttackDef;

/** @brief ラスボス戦の進行役（演出の段階・ボスの技・闘技場の回転・カメラ・HUD をまとめて持つ） */
class FinalBossBattle {
public:
    /** @brief シーンから借りる部品とプレイヤーへの攻撃の窓口 */
    struct Links {
        engine::graphics::ModelCommon* modelCommon = nullptr;
        engine::graphics::SpriteCommon* spriteCommon = nullptr;
        engine::graphics::ParticleManager* particles = nullptr;
        FontRenderer* font = nullptr;
        Input* input = nullptr;
        Player* player = nullptr;
        CameraShaker* shaker = nullptr;
        std::mt19937* rng = nullptr;
        /** @brief ボスの攻撃の判定をプレイヤーへ当てる（範囲・演出の位置・ダメージ。当たったら true） */
        std::function<bool(const AABB&, const Vector3&, int)> strikePlayer;
    };

    FinalBossBattle();
    ~FinalBossBattle();
    FinalBossBattle(const FinalBossBattle&) = delete;
    FinalBossBattle& operator=(const FinalBossBattle&) = delete;

    /** @brief モデル・分身・闘技場・HUD を用意する（ラスボス戦のレベルを開く時だけ呼ぶ） */
    void Initialize(const Links& links);
    /** @brief レベルの読み込み後に、ボスの実体を受け取って演出を始める */
    void Start(EnemyEntity* boss);
    /** @brief シーンを抜ける時に、武器の状態と入力の差し替えを元へ戻す */
    void Finalize();

    /** @brief ラスボス戦の途中か */
    bool IsActive() const {
        return phase_ != nullptr;
    }
    /** @brief 演出の途中で、プレイヤーの操作と通常の戦闘を止めるか */
    bool BlocksGameplay() const;
    /** @brief 闘技場の入力の差し替えを外す（やられ演出・クリア演出など、プレイヤーを動かさない間） */
    void ReleaseInput() {
        ClearInputOverrides();
    }
    /** @brief 闘技場のカメラで映すか（やられ演出の間は横からのカメラに戻す） */
    bool UsesArenaCamera() const {
        return IsActive();
    }

    /** @brief 操作を止めている演出の間の1フレーム（演出の段階とプレイヤーの見た目だけ進める） */
    void UpdateCinematic();
    /** @brief プレイヤーの Update の直前に呼ぶ（闘技場の前後左右の入力を横スクロールの操作へ置き換え、回り込みを反映する） */
    void PrepareInput();
    /** @brief プレイヤーの更新と戦闘の判定の後に呼ぶ（ボスの技・分身・闘技場・HUD を進める） */
    void UpdateFight(float deltaTime);
    /** @brief 闘技場のカメラの位置と向きをカメラへ書き込む */
    void ApplyCamera(engine::graphics::Camera& camera);
    /** @brief 案内文の {操作名} を今の機器の表記にする（ラスボス戦の間は本編の操作名を闘技場の操作へ読み替えてから） */
    std::wstring ExpandPrompts(const std::wstring& text) const;
    /** @brief 闘技場の操作の一覧（戦闘の前の操作説明と同じ文を、今の機器の表記にして1行ずつ） */
    std::vector<std::wstring> ControlsRows() const;
    /** @brief 闘技場の操作の一覧の見出し */
    std::wstring ControlsTitle() const;
    /** @brief 切断の破片に使う見た目（分身のモデルと、本体と重なる位置・倍率） */
    struct SliceSource {
        const engine::graphics::Model* model = nullptr;
        Vector3 position { };
        Vector3 scale { };
    };
    /** @brief 切断の破片に使う分身の見た目を返す（ラスボス戦の途中でなければ false） */
    bool GetSliceSource(SliceSource& out) const;
    /** @brief 闘技場の空の回転（空はカメラの周りに置かれているので、闘技場の回転と同じだけ回す） */
    float GetSkyYaw() const {
        return arenaYaw_;
    }

    /** @brief 闘技場・分身・撃ち出す剣・集まる武器を描く（3D の描画の途中で呼ぶ） */
    void Draw3D();
    /** @brief ボス戦の HUD を描く（スプライトの描画の途中で呼ぶ。文字はシーンの FontRenderer へ積む） */
    void DrawHud();

private:
    class IPhase;
    class GatherPhase;
    class IntroPhase;
    class GuidePhase;
    class FightPhase;
    class RagePhase;
    class DefeatPhase;
    class IMove;
    class MeasureMove;
    class RushMove;
    class IaiMove;
    class PhantomMove;
    class SpaceSlashMove;
    class BlinkMove;
    class StaggerMove;

    /** @brief カメラの狙い（目の位置と注視点） */
    struct CameraShot {
        Vector3 eye;
        Vector3 target;
    };
    /** @brief 分身に取らせる姿勢（毎フレーム技や段階が決める） */
    struct ShadowPose {
        const MeleeAttackDef* attack = nullptr; ///< 斬る段（nullptr なら構え・歩き・走り）
        float attackTime = 0.0f; ///< 段の中の経過の割合（0〜1）
        bool moving = false; ///< 走りのアニメにするか
        bool airborne = false; ///< 宙にいる姿勢にするか
        Vector3 lean { }; ///< 体に足す回転（x が負でのけぞる）
    };
    /** @brief 背後に並べて撃ち出す剣1本 */
    struct PhantomBlade {
        std::unique_ptr<engine::graphics::Object3d> object;
        Vector3 position { };
        Vector3 velocity { };
        float delay = 0.0f; ///< 撃ち出すまでの残り秒数
        float life = 0.0f; ///< 撃ち出してからの残り秒数
        bool active = false; ///< 並べている途中か飛んでいるか
        bool launched = false; ///< 撃ち出したか
    };
    /** @brief 集まって刀になる武器1本 */
    struct GatherWeapon {
        std::unique_ptr<engine::graphics::Object3d> object;
        float angle = 0.0f; ///< 回り始めの角度
    };

    // ── 進行（FinalBossBattle.cpp） ──
    /** @brief 段階を切り替える（Enter を呼ぶ） */
    void ChangePhase(const IPhase& next);
    /** @brief 闘技場の回転の中心を pivotX にして、角度 angle だけ回す（ボスやプレイヤーの周りを回り込んで見せる） */
    void RotateArenaAbout(float pivotX, float angle);
    /** @brief 闘技場の中で X 軸の上に立てる範囲（左右の端） */
    void ArenaLimits(float& outMinX, float& outMaxX) const;
    /** @brief 位置が闘技場の中に収まるか（回り込みの可否に使う） */
    bool InsideArena(const Vector3& scenePosition) const;
    /** @brief プレイヤーの立てる範囲を闘技場の中に合わせる */
    void ApplyPlayerLimits();
    /** @brief 戦闘中のプレイヤーの後ろのカメラ（cameraYaw_ の向き） */
    CameraShot FightShot() const;
    /** @brief 1フレームぶんの共通の更新（分身・ボスの実体・闘技場・HUD） */
    void UpdateCommon(float deltaTime);
    /** @brief 体力バー・名乗り・台詞などを HUD へ渡す */
    void UpdateHud(float deltaTime);
    /** @brief 分身の位置・向き・姿勢・色を反映する */
    void UpdateShadow();
    /** @brief ボスの実体の位置と向きを反映する */
    void SyncBossEntity();
    /** @brief プレイヤーの見た目の向きを進む方へ寄せる（攻撃中と止まっている時はボスを向く） */
    void UpdatePlayerFacing(float deltaTime);
    /** @brief 闘技場の入力を横スクロールの操作へ置き換える差し替えを解く */
    void ClearInputOverrides();
    /** @brief 武器を退避して刀だけにする */
    void EquipKatanaOnly();
    /** @brief 退避した武器へ戻す */
    void RestoreWeapons();

    // ── ボスの体（FinalBossMoves.cpp） ──
    /** @brief 分身に取らせる刀の地上コンボの段（段数を超えたら最後の段。表が空なら nullptr） */
    static const MeleeAttackDef* KatanaStep(int index);
    /** @brief 技の一覧を作る（Initialize から1回） */
    void BuildMoves();
    /** @brief 技を打ち切って構えへ戻す（後半戦の演出や撃破の時） */
    void CancelMoves();
    /** @brief 戦闘中の1フレーム（技を進め、終われば次の技を選ぶ） */
    void UpdateBoss(float deltaTime);
    /** @brief 次の技を間合いと後半戦かどうかで選ぶ */
    void ChooseNextMove();
    /** @brief 技を始める */
    void BeginMove(IMove& move);
    /** @brief 一定時間に受けたダメージを数え、溜まったら怯ませる */
    void UpdateStagger(float deltaTime);
    /** @brief ボスの X をプレイヤーとの重なりと闘技場の端で止める */
    float ClampBossX(float x) const;
    /** @brief プレイヤーへ向く向き（+1/-1） */
    float DirectionToPlayer() const;
    /** @brief プレイヤーとボスの距離（X 軸の上） */
    float DistanceToPlayer() const;
    /** @brief 後半戦の速さの倍率 */
    float SpeedMult() const;
    /** @brief ボスの攻撃を当てる（範囲の X は from〜to、高さはボスの体の上下） */
    bool StrikeAlong(float fromX, float toX, float centerY, int damage);
    /** @brief 撃ち出す剣を並べる（count 本、後ろの位置から） */
    void SummonPhantoms(int count);
    /** @brief 並べた剣を進め、撃ち出し、プレイヤーへ当てる */
    void UpdatePhantoms(float deltaTime);
    /** @brief 予兆の印の色（調整値の色を時計で明滅させる） */
    Vector4 TelegraphColor() const;
    /** @brief 闘技場の床の高さ（予兆の印を置く高さ） */
    float FloorY() const {
        return arena_.GetFloorY();
    }
    /** @brief 並べた剣がすべて消えたか */
    bool PhantomsCleared() const;

    Links links_;
    EnemyEntity* boss_ = nullptr;
    std::unique_ptr<Player> shadow_; ///< ボスの見た目（プレイヤーと同じモデルの黒い分身）
    FinalBossArena arena_;
    FinalBossHud hud_;
    FinalBossTelegraph telegraph_; ///< 攻撃の予兆（床の円と帯）
    float telegraphClock_ = 0.0f; ///< 予兆の印を明滅させる時計
    bool initialized_ = false;

    // 段階
    const IPhase* phase_ = nullptr;
    float phaseTimer_ = 0.0f;
    bool rageStarted_ = false; ///< 後半戦に入ったか
    bool rageCinematicDone_ = false; ///< 後半戦の演出を見せ終えたか

    // 闘技場の回転（闘技場のローカル座標 p はシーン座標 arenaCenter_ + RotY(arenaYaw_) * p）
    Vector3 arenaCenter_ { };
    float arenaYaw_ = 0.0f;

    // ボスの体
    float bossX_ = 0.0f;
    float bossLift_ = 0.0f; ///< 地面からの高さ（跳び斬り）
    float bossFacing_ = -1.0f; ///< 向き（+1=+X向き）
    float bossBaseY_ = 0.0f; ///< ボスの実体の地面での高さ（レベルの配置のまま）
    bool bossVanished_ = false; ///< 瞬身で消えている間
    ShadowPose pose_ { };
    std::vector<std::unique_ptr<IMove>> moves_;
    IMove* blinkMove_ = nullptr; ///< 怯みの後に離れる時に使う瞬身
    IMove* staggerMove_ = nullptr; ///< 怯み（技の選び直しでは選ばない）
    IMove* currentMove_ = nullptr;
    IMove* lastMove_ = nullptr;
    float thinkTimer_ = 0.0f; ///< 次の技までの残り秒数
    int lastBossHp_ = 0; ///< 前のフレームの体力（受けたダメージを数える）
    float staggerTimer_ = 0.0f; ///< ダメージを数えている残り秒数
    int staggerAccum_ = 0; ///< 数えているダメージ

    // 撃ち出す剣と、集まって刀になる武器
    std::vector<PhantomBlade> phantoms_;
    std::vector<GatherWeapon> gatherWeapons_;
    std::unique_ptr<engine::graphics::Object3d> forgedKatana_; ///< 光って生まれ、手元へ降りる刀
    bool gatherVisible_ = false; ///< 集まる武器を描くか
    bool katanaVisible_ = false; ///< 生まれた刀を描くか
    bool shadowPreviewing_ = false; ///< 分身に斬る段の姿勢を取らせているか
    bool weaponsSwapped_ = false; ///< 武器を退避して刀だけにしているか
    WeaponManager::Snapshot weaponBackup_ { };

    // プレイヤーの向きと入力
    float playerYawOffset_ = 0.0f;
    float playerHeading_ = 0.0f; ///< 進みたい方（0=ボスの方、正=右）
    bool playerMoving_ = false;
    bool overridesActive_ = false;

    // カメラと HUD
    CameraShot cameraShot_ { }; ///< 今のカメラ（狙いへなめらかに寄せる）
    CameraShot desiredShot_ { }; ///< 演出の段階が決めた狙い
    bool fightCamera_ = false; ///< プレイヤーの後ろのカメラにするか（false なら desiredShot_）
    float cameraYaw_ = 0.0f; ///< プレイヤーの後ろのカメラの向き（ボスの方へなめらかに回す）
    bool cameraPlaced_ = false;
    bool barVisible_ = false; ///< ボスの体力バーを出すか
    bool guideVisible_ = false; ///< 戦闘の前の操作説明を出すか
    float guideAlpha_ = 0.0f; ///< 操作説明の不透明度（出入りでなめらかに変える）
    float letterbox_ = 0.0f;
    float whiteVeil_ = 0.0f;
    float nameReveal_ = 0.0f;
    std::wstring line_;
    float lineAlpha_ = 0.0f;
};

/** @brief 武器の種類のモデルを読み込んで、浮かべて描く実体を作る（手に持つ武器と同じモデル） */
std::unique_ptr<engine::graphics::Object3d> CreateWeaponObject(engine::graphics::ModelCommon* modelCommon,
    WeaponType type, float scale);

} // namespace engine::game
