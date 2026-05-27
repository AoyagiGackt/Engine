#pragma once
#include "Animation.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @file AnimationStateMachine.h
 * @brief アニメーション状態マシン
 *
 * 複数のアニメーション（歩き・走り・攻撃など）を「状態」として登録し、
 * トリガーやアニメーション終了条件で自動的に遷移管理します。
 *
 * ■ 基本的な使い方
 * @code
 *   AnimationStateMachine sm;
 *
 *   // 状態を登録（loop=true にするとアニメーション終了後に先頭に戻る）
 *   sm.AddState("Idle",   &idleAnim,   true);
 *   sm.AddState("Walk",   &walkAnim,   true);
 *   sm.AddState("Attack", &attackAnim, false); // 攻撃は1回のみ
 *
 *   // 遷移ルールを登録
 *   sm.AddTransition("Idle",   "Walk",   "startWalk");
 *   sm.AddTransition("Walk",   "Idle",   "stopWalk");
 *   sm.AddTransition("Idle",   "Attack", "attack");
 *   sm.AddTransition("Walk",   "Attack", "attack");
 *   sm.AddAutoTransition("Attack", "Idle"); // Attackが終わったら自動でIdle
 *
 *   // 最初の状態をセット
 *   sm.SetState("Idle");
 *
 *   // 毎フレームの更新
 *   sm.Update(deltaTime);
 *
 *   // 入力に応じてトリガー発火
 *   if (input->PushKey(DIK_W)) { sm.Trigger("startWalk"); }
 *   if (!input->PushKey(DIK_W)) { sm.Trigger("stopWalk"); }
 *
 *   // アニメーション値の取得（SkinnedObject3d や Object3d に適用）
 *   const Animation* anim = sm.GetCurrentAnimation();
 *   float t = sm.GetCurrentTime();
 *   NodeAnimation& nodeAnim = anim->nodeAnimations.at("Bone_001");
 *   Vector3    pos = CalculateValue(nodeAnim.translate, t);
 *   Quaternion rot = CalculateValue(nodeAnim.rotate,    t);
 * @endcode
 */
class AnimationStateMachine {
public:
    AnimationStateMachine() = default;

    // =============================================
    // 状態の登録
    // =============================================

    /**
     * @brief 状態を追加する
     * @param name  状態の名前（一意）
     * @param anim  アニメーションデータへのポインタ（寿命はユーザーが管理）
     * @param loop  true=ループ再生 / false=1回再生して停止
     * @param speed 再生速度の倍率（デフォルト 1.0）
     */
    void AddState(const std::string& name, Animation* anim, bool loop = true, float speed = 1.0f);

    // =============================================
    // 遷移ルールの登録
    // =============================================

    /**
     * @brief トリガー名による遷移を追加する
     * @param from    遷移元の状態名
     * @param to      遷移先の状態名
     * @param trigger Trigger() で指定するトリガー名
     */
    void AddTransition(const std::string& from, const std::string& to, const std::string& trigger);

    /**
     * @brief アニメーション終了時の自動遷移を追加する（loop=false の状態向け）
     * @param from 遷移元（loop=false であること）
     * @param to   遷移先
     */
    void AddAutoTransition(const std::string& from, const std::string& to);

    // =============================================
    // 制御
    // =============================================

    /**
     * @brief 初期状態または強制状態変更を行う
     * @note 遷移ルールを無視して即座に切り替わる
     */
    void SetState(const std::string& name);

    /**
     * @brief トリガーを発火する
     * @note 現在の状態からトリガー名に対応する遷移があれば遷移する。
     *       対応する遷移がない場合は何もしない。
     */
    void Trigger(const std::string& triggerName);

    /**
     * @brief 毎フレームの更新処理
     * @param dt デルタタイム（秒）
     *
     * アニメーション時刻を進め、loop=false の状態でアニメーションが
     * 終了した場合に自動遷移を実行する。
     */
    void Update(float dt);

    // =============================================
    // 情報取得
    // =============================================

    /** @brief 現在の状態名を返す */
    const std::string& GetCurrentStateName() const { return currentState_; }

    /** @brief 現在のアニメーション再生時刻を返す */
    float GetCurrentTime() const { return currentTime_; }

    /**
     * @brief 現在の Animation データへのポインタを返す
     * @return nullptr は状態が未セットの場合
     */
    const Animation* GetCurrentAnimation() const;

    /**
     * @brief 現在の状態が指定した名前かどうかを返す
     */
    bool IsInState(const std::string& name) const { return currentState_ == name; }

    /**
     * @brief 現在の状態のアニメーションが終端まで達したか（loop=false の場合のみ有効）
     */
    bool IsCurrentAnimationFinished() const;

    /**
     * @brief 登録されている状態の数を返す（デバッグ用）
     */
    size_t GetStateCount() const { return states_.size(); }

private:
    // ---- 状態データ ----
    struct State {
        std::string name;
        Animation*  anim  = nullptr;
        bool        loop  = true;
        float       speed = 1.0f;
        std::string autoTransitionTo; ///< loop=false 終了後に遷移する先（空文字=自動遷移なし）
    };

    // ---- 遷移ルール ----
    struct Transition {
        std::string from;
        std::string to;
        std::string trigger;
    };

    std::unordered_map<std::string, State> states_;
    std::vector<Transition>                transitions_;

    std::string currentState_;
    float       currentTime_ = 0.0f;

    // ---- 内部遷移処理 ----
    void TransitionTo(const std::string& stateName);
};
