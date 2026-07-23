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
