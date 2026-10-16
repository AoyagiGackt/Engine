/**
 * @file PlayerTuning.h
 * @brief プレイヤーの移動・回避・覚醒・固有技の調整値を Resources/Config/player_params.json から読む設定
 * @note 値を変えてもコードのリビルドが要らないようにする。キーとメンバの対応は PlayerTuning.cpp の表が持つ
 */
#pragma once
namespace engine::game {

/** @brief プレイヤーの調整値（初期値はJSONに項目が無い時の既定値） */
struct PlayerTuning {
    // 移動
    float groundY = 0.4f; ///< 配置床が無いステージの地面の高さ
    float ceilingY = 12.0f;
    float gravity = 0.012f;
    float jumpPower = 0.4f;
    float speed = 0.15f; ///< 1フレームあたりの横移動量
    float minEnemyDistanceX = 0.75f; ///< 敵とめり込まないための最小X距離（接触ダメージの重なりしろは残す）
    float minEnemyDistanceY = 0.75f; ///< 上記のY版（頭上を飛び越えている間は押し出さない）
    float invincibleDuration = 1.0f; ///< 被弾後の無敵秒数

    // 水中
    float waterGravity = 0.003f; ///< 浮力で弱めた沈下加速度
    float waterSpeed = 0.10f;
    float swimAccel = 0.025f; ///< 長押しで上昇する加速度
    float swimMaxVY = 0.28f;
    float sinkMaxVY = -0.12f;
    float waterEntryImpactDamping = 0.4f; ///< 入水時に落下速度へ掛ける倍率

    // 回避
    int dodgeBufferFrames = 8; ///< 回避の先行入力を保持するフレーム数
    float dodgeDistance = 2.6f;
    float dodgeDuration = 0.22f; ///< 全区間無敵
    float dodgeCooldown = 0.30f;
    float dodgeSpamWindow = 1.2f; ///< この秒数以内の次の回避は連打扱い
    float dodgeAnimSpeed = 2.4f;
    float backDodgeAnimSpeed = 1.6f;
    float backDodgeLean = -0.45f; ///< バックステップで後ろへ反る最大角度（ラジアン）
    float backDodgeHopHeight = 0.25f; ///< バックステップで見た目だけ浮かせる高さ

    // テレポート斬り
    float warpDistance = 4.0f;
    float warpAwakenedDistance = 6.0f;
    float warpDuration = 0.12f;
    float warpCooldown = 0.9f;
    float warpAwakenedCooldown = 0.35f;
    float warpGaugeCost = 0.08f; ///< 通常時に消費する覚醒ゲージ量
    float warpAnimSpeed = 2.0f;

    // 覚醒
    float awakenDuration = 8.0f;
    float awakenActivationThreshold = 0.3f; ///< このゲージ量が溜まっていれば発動できる
    float awakenedSpeedMult = 1.5f;
    float awakenedJumpMult = 1.3f;
    int awakenExtraHits = 2; ///< 覚醒中に1段ごとに出す追加ヒットの回数
    float awakenExtraHitInterval = 0.045f;

    // 近接コンボ
    float weaponSwitchWindow = 0.75f; ///< 持ち替え直後の攻撃が持ち替え攻撃になる猶予
    float weaponSwitchLunge = 0.8f;
    float launchFollowWindow = 0.75f; ///< 打ち上げ後に追撃ジャンプが強化される秒数
    float launchFollowJumpMult = 1.5f;
    float airAttackFallDamping = 0.35f; ///< 空中攻撃中に落下速度へ掛ける倍率
    float attackAnimSpeed = 1.5f; ///< 乱舞の自動斬撃のモーション速度
    float finisherReleaseAnimSpeed = 3.0f;
    float stealStabAnimSpeed = 1.2f;

    // 覚醒乱舞
    float rampageSpeed = 0.45f; ///< 打ち上げ突進の速度
    float rampageReachDistance = 1.0f; ///< この距離まで近づいたら打ち上げる
    float rampageLaunchSpeed = 0.25f;
    float juggleRadius = 2.5f; ///< 敵からのスラッシュ距離
    int juggleMaxSlashes = 8;

    // 固有技
    float dashDuration = 0.10f; ///< 踏み込み・突進系の技が移動しきる秒数
    float skillHoldHeight = 0.5f; ///< 投げた武器を浮かせる高さ（胸の高さ）
    float daggerAirDashDist = 3.4f;
    float daggerAirDashAnimSpeed = 2.0f;
    float daggerSkillCooldown = 4.0f;
    float swordSkillCooldown = 5.0f;
    float spearSkillCooldown = 4.5f;
    float hammerSkillCooldown = 5.0f;
    float ballShootInterval = 0.12f;
    float ballSpinSpeed = 5.0f; ///< 空中回転の速さ（度/フレーム）
    float ballSpinShotAnimSpeed = 2.2f;
    float greatswordThrowDist = 4.0f;
    float greatswordThrowTravelTime = 0.12f;
    float greatswordVortexMaxDuration = 10.0f; ///< 再入力が無くても渦が終わる上限
    float greatswordSpinHitInterval = 0.15f;
    float greatswordThrowCooldown = 1.6f;
    float greatswordReturnTime = 0.2f;
    float greatswordThrowAnimSpeed = 1.3f;
    float greatswordThrowSpinSpeed = 22.0f; ///< 投げた大剣の回転速度（ラジアン/秒）
    float greatswordVortexBobAmplitude = 0.06f;
    float greatswordVortexBobSpeed = 10.0f;
    float scytheHoverMax = 0.9f; ///< 最大連続ホバー秒数
    float scytheHoverRecoverRate = 2.0f; ///< 接地中の回復速度倍率
    float scytheHoverVYCap = -0.02f; ///< ホバー中の落下速度の下限
    float scytheHoverAnimSpeed = 1.25f;
    float axeChargeDist = 2.3f;
    float axeSkillCooldown = 1.0f;
    float axeRageDuration = 2.5f;
    float axeRageDamageMult = 1.3f;
    float axeChargeAnimSpeed = 1.1f;

    /** @brief 読み込み済みの調整値を返す（初回はJSONを読む） */
    static const PlayerTuning& Get();
    /** @brief JSONを読み直す */
    static void Reload();
};

} // namespace engine::game
