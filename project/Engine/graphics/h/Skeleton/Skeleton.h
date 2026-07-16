#pragma once
#include <MakeAffine.h>
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace engine::graphics {
struct EulerTransform {
    Vector3 scale;
    Vector3 rotate;
    Vector3 translate;
};

struct QuaternionTransform {
    Vector3 scale;
    Quaternion rotate;
    Vector3 translate;
};

struct Node {
    QuaternionTransform transform;
    Matrix4x4 localMatrix;
    std::string name;
    std::vector<Node> children;
};

// 1本の骨（ジョイント）
struct Joint {
    QuaternionTransform transform; // ローカルのSRT
    Matrix4x4 localMatrix; // ローカル行列
    Matrix4x4 skeletonSpaceMatrix; // スケルトン空間での累積行列
    std::string name;
    std::optional<int32_t> parent; // 親JointのIndexrootならnullopt
    int32_t index; // joints配列内のIndex
};

class Skeleton {
public:
    // Nodeの階差構造からSkeletonを生成する
    static Skeleton Create(const Node& rootNode);

    // localMatrixを反映してskeletonSpaceMatrixを更新する
    void Update();

    // ImGuiでSkeletonの状態をデバッグ描画する（自動スケールのスティックフィギュア）
    void DebugDraw();

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
