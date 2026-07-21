/**
 * @file Skeleton.h
 * @brief Skeletonの描画資源とGPU処理の管理に関する公開型と操作インターフェースを定義するファイル
 */
#pragma once
#include <MakeAffine.h>
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace engine::graphics {
/**
 * @brief EulerTransform に関する型を提供する
 * @details EulerTransform が扱うデータと操作の責務をまとめる
 */
struct EulerTransform {
    Vector3 scale;
    Vector3 rotate;
    Vector3 translate;
};

/**
 * @brief QuaternionTransform に関する型を提供する
 * @details QuaternionTransform が扱うデータと操作の責務をまとめる
 */
struct QuaternionTransform {
    Vector3 scale;
    Quaternion rotate;
    Vector3 translate;
};

/**
 * @brief Node に関する型を提供する
 * @details Node が扱うデータと操作の責務をまとめる
 */
struct Node {
    QuaternionTransform transform;
    Matrix4x4 localMatrix;
    std::string name;
    std::vector<Node> children;
};

// 1本の骨（ジョイント）
/**
 * @brief Joint に関する型を提供する
 * @details Joint が扱うデータと操作の責務をまとめる
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
 * @brief Skeleton に関する型を提供する
 * @details Skeleton が扱うデータと操作の責務をまとめる
 */
class Skeleton {
public:
    // Nodeの階差構造からSkeletonを生成する
    /**
     * @brief Create の結果を取得する
     * @param rootNode 処理に使用する値
     * @return 処理結果
     */
    static Skeleton Create(const Node& rootNode);

    // localMatrixを反映してskeletonSpaceMatrixを更新する
    /**
     * @brief Update に対応する状態を更新する
     * @return なし
     */
    void Update();

    // ImGuiでSkeletonの状態をデバッグ描画する（自動スケールのスティックフィギュア）
    /**
     * @brief DrawDiagnostics に対応する内容を描画する
     * @return なし
     */
    void DrawDiagnostics();

    /** @brief root JointのIndexを返す */
    int32_t GetRoot() const { return root_; }
    /** @brief Joint名 → Index のマップを返す */
    const std::map<std::string, int32_t>& GetJointMap() const { return jointMap_; }
    /** @brief 全Jointのフラット配列を返す（読み取り専用） */
    const std::vector<Joint>& GetJoints() const { return joints_; }
    /** @brief 全Jointのフラット配列を返す（アニメーション適用など書き込み用） */
    std::vector<Joint>& GetJoints() { return joints_; }

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
