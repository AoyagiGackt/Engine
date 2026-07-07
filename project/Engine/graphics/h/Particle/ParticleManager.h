/**
 * @file ParticleManager.h
 * @brief Compute Shader でパーティクルを GPU 完結シミュレーションし、インスタンシング描画するファイル
 */
#pragma once
#include "Camera.h"
#include "DirectXCommon.h"
#include "MakeAffine.h"
#include "Model.h"
#include "SrvManager.h"
#include <array>
#include <d3d12.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <wrl/client.h>
namespace engine::graphics {

/**
 * @brief GPU 側で保持するパーティクル 1 粒のシミュレーションデータ (CS が読み書き)
 */
struct GPUParticleState {
    Vector3  position;    // 12
    float    lifeTime;    //  4 -> 16
    Vector3  velocity;    // 12
    float    currentTime; //  4 -> 32
    Vector4  color;       // 16 -> 48
    Vector3  scale;       // 12
    float    rotateZ;     //  4 -> 64
    uint32_t alive;       //  4
    uint32_t curveFlag;   //  4  (1 = enemyDeath 螺旋)
    float    pad[2];      //  8 -> 80
};

/**
 * @brief 描画のために VS が読むインスタンシングデータ (CS が書き込み)
 */
struct ParticleForGPU {
    Matrix4x4 WVP;
    Matrix4x4 World;
    Vector4   color;
};

/**
 * @brief CS に渡す定数バッファの内容
 */
struct CSConstants {
    Matrix4x4 billboard;    // 64
    Matrix4x4 viewProj;     // 64
    float     deltaTime;    //  4
    uint32_t  maxParticles; //  4
    float     pad[2];       //  8 -> 144
};

/**
 * @brief EmitParticle.CS に渡すエミッター定数バッファ（UPLOAD heap 256 bytes に格納）
 *        CPU 側で frequencyTime / emit を毎フレーム更新する
 */
struct Emitter {
    Vector3  translate;     // 12
    float    radius;        //  4 -> 16
    uint32_t count;         //  4
    float    frequency;     //  4
    float    frequencyTime; //  4
    uint32_t emit;          //  4 -> 32
    float    lifeTime;      //  4
    uint32_t seed;          //  4
    uint32_t time;          //  4  (groupTime の float ビット列、Update が自動設定)
    float    pad;           //  4 -> 48
    Vector4  color;         // 16 -> 64
};

/**
 * @brief 同じテクスチャを共有するパーティクルの集まり（グループ）
 */
struct ParticleGroup {
    std::string textureFilePath;

    // GPU シミュレーションステートバッファ (DEFAULT heap, UAV)
    Microsoft::WRL::ComPtr<ID3D12Resource> particleStateBuffer;

    // CPU→GPU コピー用ステージングバッファ (UPLOAD heap, 常時マップ済み)
    Microsoft::WRL::ComPtr<ID3D12Resource> particleUploadBuffer;
    GPUParticleState* particleUploadData = nullptr;

    // インスタンシング描画バッファ (DEFAULT heap, CS が UAV として書き、VS が SRV として読む)
    uint32_t srvIndex = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource;

    static constexpr uint32_t kNumMaxInstance = 1024;

    // CPU 側スロット管理 (GPU readback 不要)
    std::array<float, kNumMaxInstance> slotExpiry = {};
    float groupTime = 0.0f;

    // 現在生存中のスロット数（Draw の hasAlive スキャンを O(1) にする）
    uint32_t aliveCount = 0;

    // 空きスロットのスタック（O(1) Allocate）
    std::vector<uint32_t> freeList;

    // このフレームに新規発生したスロット一覧 (次の Update で GPU にコピーする)
    std::vector<uint32_t> pendingSlots;

    // instancingResource の現在状態
    bool instancingInSRV      = false; // false=UAV  true=NON_PIXEL_SHADER_RESOURCE
    bool needsInit            = true;  // 初回 Update で全スロットをゼロ初期化する
    bool particleStateFresh   = true;  // particleStateBuffer が作成直後の COMMON 状態か
                                       // 最初の UAV→X バリアで false にセット
    bool additiveBlend   = true;    // false = alpha blend (SRC_ALPHA / INV_SRC_ALPHA)

    // デフォルト寿命（EmitBurst で設定される）
    float defaultLifeTime = 1.0f;

    // GPU エミッター (EmitParticle.CS)
    Microsoft::WRL::ComPtr<ID3D12Resource> emitterBuffer;
    Emitter* emitterData = nullptr;

    // CPU 自動再配置（EmitScatterLoop 用）
    struct RespawnConfig {
        Vector3  center      = {};
        float    radius      = 0.0f;
        float    lifeTimeMin = 1.0f;
        float    lifeTimeMax = 1.0f;
        Vector4  color       = { 1.0f, 1.0f, 1.0f, 1.0f };
        float    scale       = 1.0f;
        uint32_t count       = 0;
    };
    bool          autoRespawn  = false;
    RespawnConfig respawnConfig;
};

/**
 * @brief パーティクル全体を管理し、CS でシミュレーション・インスタンシング描画を行うシングルトン
 */
class ParticleManager {
public:
    /** @brief ParticleManager の唯一のインスタンスを取得する */
    static ParticleManager* GetInstance();

    /**
     * @brief GPU リソース・パイプラインを作成して初期化する
     * @param dxCommon デバイス・コマンドリスト取得に使用する DirectX 基盤
     */
    void Initialize(engine::DirectXCommon* dxCommon);

    /** @brief 全グループを破棄し GPU リソースを解放する */
    void Finalize();

    /**
     * @brief 全グループの Compute Shader をディスパッチしてパーティクルを更新する
     * @param camera ビルボード行列・VP 行列の計算に使用するカメラ
     */
    void Update(Camera* camera);

    /**
     * @brief 生存中のパーティクルをインスタンシング描画する
     * @param camera 使用カメラ（現在は未使用、将来の視錐台カリング用に保持）
     */
    void Draw(Camera* camera);

    /**
     * @brief 白色・デフォルトパラメーターで1粒放出する
     * @param name     対象グループ名
     * @param position 放出位置
     * @param velocity 初速ベクトル
     */
    void Emit(const std::string& name, const Vector3& position, const Vector3& velocity);

    /**
     * @brief 色・寿命・スケールを指定して1粒放出する
     * @param flicker true にすると明滅エフェクト（curveFlag=2）を適用する
     */
    void EmitWithColor(const std::string& name, const Vector3& position,
        const Vector3& velocity, const Vector4& color,
        float lifeTime = 1.0f, float scale = 1.0f, bool flicker = false);

    /**
     * @brief 横長の楕円形スケールで1粒放出する（斬撃の残光等に使用）
     */
    void EmitEllipse(const std::string& name, const Vector3& position,
        const Vector3& velocity, const Vector4& color,
        float lifeTime = 1.0f, float scaleX = 2.0f, float scaleY = 1.0f);

    /**
     * @brief 斬撃の剣閃（残光＋芯＋斬線に沿って抜ける光片）を放出する
     * @param angle  斬撃の角度（ラジアン）
     * @param radius 斬線の半長（ワールド単位）
     */
    void EmitSlash(const std::string& name, const Vector3& position,
        float angle, const Vector4& color, float radius = 1.0f);

    /**
     * @brief ヒット時の星形エフェクト用にランダム方向・スケールで複数粒放出する
     */
    void EmitHitStar(const std::string& name, const Vector3& position, const Vector4& color);

    /**
     * @brief 指定数のパーティクルをランダム位置に一斉配置する（環境パーティクル等）
     * @param count    放出数（最大 kNumMaxInstance）
     * @param lifeTime 各パーティクルの寿命（デフォルトは実質無限）
     */
    void EmitBurst(const std::string& name, const Vector3& position, const Vector4& color,
        uint32_t count = ParticleGroup::kNumMaxInstance,
        float lifeTime = 100000.0f, float scale = 1.0f, bool flicker = false);

    /**
     * @brief 指定半径・高さにランダム配置し、寿命が尽きると自動再配置するループエフェクトを開始する
     */
    void EmitScatterLoop(const std::string& name, const Vector3& center, float radius,
        uint32_t count, const Vector4& color,
        float lifeTimeMin, float lifeTimeMax, float scale);

    /**
     * @brief 初速を与えて重力落下するパーティクルを1粒放出する（curveFlag=3）
     */
    void EmitGravity(const std::string& name, const Vector3& position,
        const Vector3& velocity, const Vector4& color,
        float lifeTime = 0.6f, float scale = 0.2f);

    /**
     * @brief 均等角度で放射状にリングエフェクトを放出する（ヒットストップ等）
     */
    void EmitRing(const std::string& name, const Vector3& position,
        float speed, const Vector4& color,
        uint32_t count = 16, float lifeTime = 0.4f, float scale = 0.3f);

    /**
     * @brief 残像1粒を放出する。毎フレーム呼び続けることでトレイルを形成する
     */
    void EmitTrail(const std::string& name, const Vector3& position,
        const Vector4& color, float scale = 0.5f, float lifeTime = 0.15f);

    /**
     * @brief 指定グループのテクスチャを差し替える
     * @param groupName       変更対象のグループ名
     * @param textureFilePath 新しいテクスチャのファイルパス
     */
    void SetTexture(const std::string& groupName, const std::string& textureFilePath);

    /**
     * @brief 新しいパーティクルグループを生成・登録する
     * @param name            グループの識別名（重複不可）
     * @param textureFilePath 使用テクスチャのファイルパス
     */
    void CreateParticleGroup(const std::string& name, const std::string& textureFilePath);

    /** @brief 全パーティクルグループを破棄する */
    void ClearAllGroups()       { particleGroups_.clear(); }

    /**
     * @brief グループの加算合成・アルファ合成を切り替える
     * @param additive true=加算合成, false=アルファ合成
     */
    void SetAdditiveBlend(const std::string& name, bool additive);

    /**
     * @brief 指定グループに生存中のパーティクルが1粒以上存在するかを返す
     * @return true=生存あり / false=全滅
     */
    bool IsGroupAlive(const std::string& name) const;

    /**
     * @brief 指定グループのエミッターデータへのポインタを返す
     * @note 呼び出し側で translate/radius/count/frequency/lifeTime を直接設定する
     */
    Emitter* GetEmitter(const std::string& name);

private:
    ParticleManager() = default;
    ~ParticleManager() = default;
    ParticleManager(const ParticleManager&) = delete;
    ParticleManager& operator=(const ParticleManager&) = delete;

    // パイプライン構築
    void CreateRootSignature();
    void CreatePipelineState();
    void CreateCSRootSignature();
    void CreateCSPipelineState();
    void CreateCSEmitRootSignature();
    void CreateCSEmitPipelineState();
    void CreateQuadGeometry();

    // 空きスロットを返す。なければ UINT32_MAX
    uint32_t AllocateSlot(ParticleGroup& group);

    // CreateParticleGroup のフェーズ分割ヘルパー
    void CreateParticleStateBuffers(ParticleGroup& group);
    void CreateParticleInstancingResource(ParticleGroup& group);
    void InitParticleGroupState(ParticleGroup& group);

    // Update のフェーズ分割ヘルパー
    void UpdateCSConstants(Camera* camera, float dt);
    void TransitionInstancingToUAV(ParticleGroup& group, ID3D12GraphicsCommandList* cmd);
    void ExpireAndRespawnSlots(ParticleGroup& group);
    void FlushPendingSlotsToGPU(ParticleGroup& group, ID3D12GraphicsCommandList* cmd);
    void DispatchEmitCS(ParticleGroup& group, ID3D12GraphicsCommandList* cmd, float dt);
    void DispatchUpdateCS(ParticleGroup& group, ID3D12GraphicsCommandList* cmd);

private:
    engine::DirectXCommon* dxCommon_ = nullptr;

    // quad ジオメトリ（全グループ共有）
    Microsoft::WRL::ComPtr<ID3D12Resource> quadVertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> quadIndexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW quadVBV_{};
    D3D12_INDEX_BUFFER_VIEW  quadIBV_{};

    // グラフィックスパイプライン
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState_;        // additive blend
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineStateAlpha_;   // alpha blend

    // Compute パイプライン (Update)
    Microsoft::WRL::ComPtr<ID3D12RootSignature> csRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> csPipelineState_;

    // Compute パイプライン (Emit)
    Microsoft::WRL::ComPtr<ID3D12RootSignature> csEmitRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> csEmitPipelineState_;

    // フレームごとの CS 定数バッファ (UPLOAD heap, 常時マップ済み)
    Microsoft::WRL::ComPtr<ID3D12Resource> csConstantsBuffer_;
    CSConstants* csConstantsData_ = nullptr;

    std::unordered_map<std::string, ParticleGroup> particleGroups_;
};

} // namespace engine::graphics
