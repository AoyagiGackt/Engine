/**
 * @file Skeleton.h
 * @brief スケルトン（関節の階層）の型と操作の宣言
 */
#pragma once
#include <MakeAffine.h>
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace engine::graphics {
/**
 * @brief オイラー角（度数ではなくラジアン想定）による SRT 変換
 */
struct EulerTransform {
    Vector3 scale;
    Vector3 rotate;
    Vector3 translate;
};

/**
 * @brief クォータニオン回転による SRT 変換（ジョイント・アニメーションが実際に使う形式）
 */
struct QuaternionTransform {
    Vector3 scale;
    Quaternion rotate;
    Vector3 translate;
};

/**
 * @brief assimp から読み込んだシーンノードの階層構造（Skeleton::Create の入力元）
 */
struct Node {
    QuaternionTransform transform;
    Matrix4x4 localMatrix;
    std::string name;
    std::vector<Node> children;
};

// 1本の骨（ジョイント）
/**
 * @brief スケルトン内の1ジョイント（骨）。ローカル変換と親子関係、累積行列を保持する
 */
struct Joint {
    QuaternionTransform transform; // ローカルのSRT
    Matrix4x4 localMatrix; // ローカル行列
    Matrix4x4 skeletonSpaceMatrix; // スケルトン空間での累積行列
    std::string name;
    std::optional<int32_t> parent; // 親JointのIndexrootならnullopt
    int32_t index; // joints配列内のIndex
};

/**
 * @brief ジョイントの階層とスケルトン空間行列を保持し、毎フレーム行列更新を行うスケルトンクラス
 */
class Skeleton {
public:
    // Nodeの階差構造からSkeletonを生成する
    /**
     * @brief Node階層をDFSで辿り、親子関係を保持したJoint配列としてSkeletonを構築する
     * @param rootNode 変換元となるノード階層のルート（LoadNodeHierarchyFromFile で読み込んだもの）
     * @return 構築されたSkeletonインスタンス（skeletonSpaceMatrixは未更新の単位行列）
     */
    static Skeleton Create(const Node& rootNode);

    // localMatrixを反映してskeletonSpaceMatrixを更新する
    /**
     * @brief 全JointのlocalMatrixを再計算し、親から順にskeletonSpaceMatrix（累積行列）へ反映する
     */
    void Update();

    // ImGuiでSkeletonの状態をデバッグ描画する（自動スケールのスティックフィギュア）
    /**
     * @brief ImGuiウィンドウに全ジョイントを2D投影したスティックフィギュアとツリービューを表示する
     */
    void DrawDiagnostics();

    /** @brief root JointのIndexを返す */
    int32_t GetRoot() const {
        return root_;
    }
    /** @brief 名前に一致するジョイント番号を返す。未登録なら値を持たない */
    std::optional<int32_t> FindJointIndex(const std::string& name) const
    {
        const auto found = jointMap_.find(name);
        return found != jointMap_.end() ? std::optional<int32_t>(found->second) : std::nullopt;
    }
    /** @brief 指定名のジョイントを持つか調べる */
    bool HasJoint(const std::string& name) const
    {
        return FindJointIndex(name).has_value();
    }
    /** @brief ジョイント数を返す */
    size_t GetJointCount() const
    {
        return joints_.size();
    }
    /** @brief 指定番号のジョイントだけを読み取り専用で参照する */
    const Joint& GetJoint(size_t index) const
    {
        return joints_.at(index);
    }
    /** @brief 指定ジョイントのローカル変換だけを更新する */
    void SetJointTransform(size_t index, const QuaternionTransform& transform) {
        joints_.at(index).transform = transform;
    }
    /** @brief 指定ジョイントのローカル回転だけを更新する */
    void SetJointRotation(size_t index, const Quaternion& rotation) {
        joints_.at(index).transform.rotate = rotation;
    }

private:
    static int32_t CreateJoint(
        const Node& node,
        const std::optional<int32_t>& parent,
        std::vector<Joint>& joints,
        std::map<std::string, int32_t>& jointMap);

    int32_t root_ = 0; // root JointのIndex
    std::map<std::string, int32_t> jointMap_; // Joint名 → Index
    std::vector<Joint> joints_; // 全Jointのフラット配列
};

// assimpを使ってGLTFファイルからNodeの階差構造を読み込む
Node LoadNodeHierarchyFromFile(const std::string& directoryPath, const std::string& filename);

} // namespace engine::graphics
