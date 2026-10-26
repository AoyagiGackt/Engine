/**
 * @file ComboMotion.h
 * @brief コンボの段ごとにボーン回転のキーを持ち、ベースアニメへ重ねる加算モーションのデータと再生レイヤーを定義するファイル
 */
#pragma once
#include <functional>
#include "MakeAffine.h"
#include "PoseLayer.h"
#include "Quaternion.h"
#include <map>
#include <string>
#include <vector>

namespace engine::game {

/** @brief 回転キー1つ（段の経過割合とその時点の差分角度） */
struct MotionKey {
    float time = 0.0f; ///< 段の経過割合（0〜1）
    Vector3 eulerDeg = { }; ///< ボーンのローカル軸での差分回転（度、XYZ順）
};

/** @brief 1ボーンぶんの回転キー列 */
struct MotionTrack {
    std::string bone; ///< 対象のボーン名（リグの実際の名前）
    std::vector<MotionKey> keys; ///< 時刻順に並べたキー

    /** @brief 指定時刻の差分回転を返す（キー間は最短経路の球面補間、範囲外は端のキーを保つ） */
    Quaternion Evaluate(float time) const;

    /** @brief 指定時刻の差分角度を返す（エディタで新しいキーの初期値に使う） */
    Vector3 EvaluateEuler(float time) const;

    /**
     * @brief 指定時刻のキーを追加または上書きする
     * @param time      段の経過割合
     * @param eulerDeg  差分角度（度）
     * @param snap      この時刻差以内に既存キーがあればそれを上書きする
     * @return 追加・上書きしたキー
     */
    void SetKey(float time, const Vector3& eulerDeg, float snap);

    /** @brief 指定時刻に近いキーを返す（無ければnullptr） */
    MotionKey* FindKey(float time, float snap);

    /** @brief 指定時刻に近いキーを消す */
    void RemoveKey(float time, float snap);

    /** @brief キーを時刻順に並べ直す（時刻を直接書き換えた後に呼ぶ） */
    void SortKeys();
};

/** @brief 1段ぶんの加算モーション（複数ボーンのトラック） */
struct MotionClip {
    std::vector<MotionTrack> tracks; ///< ボーンごとのトラック

    /** @brief 指定ボーンのトラックを返す（無ければnullptr） */
    MotionTrack* FindTrack(const std::string& bone);
    /** @brief 指定ボーンのトラックを返す（無ければnullptr、読み取り専用） */
    const MotionTrack* FindTrack(const std::string& bone) const;
    /** @brief 指定したボーンと時刻へキーを追加または上書きする */
    void SetBoneKey(const std::string& bone, float time, const Vector3& eulerDeg, float snap);
    /** @brief 指定ボーンのトラックをキーごと消す */
    void RemoveTrack(const std::string& bone);
private:
    /** @brief 内部操作用のトラックを確保し、初回は始端と終端を無回転にする */
    MotionTrack& EnsureTrack(const std::string& bone);
};

/**
 * @brief 加算モーションの保管庫（Resources/Config/combo_motion.json）
 * @note リグ（モデルファイル名）ごとに、段の技ID（MeleeAttackDef::id）をキーとしてクリップを持つ。
 *       リグが違うとボーン名や軸が違うため、同じ技でもリグごとに別のクリップにする。
 */
class ComboMotionLibrary {
public:
    /** @brief 共有の保管庫を返す（初回に JSON を読み込む） */
    static ComboMotionLibrary* GetInstance();

    /**
     * @brief JSONを読み直す
     * @return 読めたらtrue（ファイルが無い・形が違う時はfalseで、今のデータを残す）
     */
    bool Load();
    /**
     * @brief JSONへ書き出す
     * @return 書き込んだ内容を読み戻して一致すればtrue
     */
    bool Save() const;

    /** @brief クリップを返す（無ければnullptr） */
    const MotionClip* Find(const std::string& rigId, const std::string& attackId) const;
    /** @brief 編集用コピーへ操作を適用してから保管庫へ反映する */
    void EditClip(const std::string& rigId, const std::string& attackId,
        const std::function<void(MotionClip&)>& edit);

    /** @brief 加算モーションをゲーム中に掛けるか（比較用に切り替える） */
    bool IsEnabled() const {
        return enabled_;
    }
    /** @brief 加算モーションを掛けるかを切り替える */
    void SetEnabled(bool enabled) {
        enabled_ = enabled;
    }

private:
    /** @brief 生成時に JSON を読み込む */
    ComboMotionLibrary();

    std::map<std::string, std::map<std::string, MotionClip>> clips_; ///< [リグ][技ID]
    bool enabled_ = true;
};

/**
 * @brief 加算モーションを再生するポーズレイヤー
 * @note 毎フレーム SetClip() で今の段のクリップと経過割合を渡し、SkinnedObject3d に評価してもらう。
 */
class ComboMotionLayer : public engine::graphics::IPoseLayer {
public:
    /**
     * @brief 掛けるクリップと時刻を設定する
     * @param clip           掛けるクリップ（nullptrで何も掛けない）
     * @param normalizedTime 段の経過割合（0〜1）
     */
    void SetClip(const MotionClip* clip, float normalizedTime) {
        clip_ = clip;
        time_ = normalizedTime;
    }

    /** @brief 設定されたクリップの各トラックを今の時刻で評価し、リグにあるボーンの差分回転を集める */
    void Collect(const engine::graphics::Skeleton& skeleton,
        std::vector<engine::graphics::PoseLayerRotation>& out) const override;

    /** @brief 直近の評価で掛けたクリップのトラック数（クリップが無ければ0） */
    int GetLastTrackCount() const {
        return lastTrackCount_;
    }
    /** @brief 直近の評価で、リグにボーンが見つかって実際に掛けたトラック数 */
    int GetLastAppliedCount() const {
        return lastAppliedCount_;
    }

private:
    const MotionClip* clip_ = nullptr;
    float time_ = 0.0f;
    // 確認用の数（ボーン名の食い違いなどで掛かっていない時に気付けるようにする）
    mutable int lastTrackCount_ = 0;
    mutable int lastAppliedCount_ = 0;
};

} // namespace engine::game
