#include "TrainingScene.h"
#include "DebugProfiler.h"
#include "GameConstants.h"
#include "SceneManager.h"
#include "SSAOEffect.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#ifdef USE_IMGUI
#include <imgui.h>
#endif

static constexpr float kWarpX        = 25.5f;
static constexpr float kWarpProximity = 3.0f;

void TrainingScene::Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio)
{
    dxCommon_ = dxCommon;
    input_    = input;
    audio_    = audio;

    srvManager_    = SrvManager::GetInstance();
    weaponManager_ = WeaponManager::GetInstance();

    spriteCommon_ = std::make_unique<SpriteCommon>();
    spriteCommon_->Initialize(dxCommon_);

    modelCommon_ = std::make_unique<ModelCommon>();
    modelCommon_->Initialize(dxCommon_);

    objectCommon_ = std::make_unique<Object3dCommon>();
    objectCommon_->Initialize(dxCommon_);

    shadowManager_ = std::make_unique<ShadowManager>();
    shadowManager_->Initialize(dxCommon_, srvManager_);

    camera_ = std::make_unique<Camera>();
    camera_->SetTranslate({ 14.5f, 6.0f, -30.0f });
    Object3d::SetCommonCamera(camera_.get());

    modelBlock_ = std::make_unique<Model>();
    modelBlock_->Initialize(modelCommon_.get(),
        "Resources/block/block.obj",
        "Resources/block/block.png");

    auto addBlock = [&](float x, float y, float z) {
        auto b = std::make_unique<Object3d>();
        b->Initialize(modelCommon_.get());
        b->SetModel(modelBlock_.get());
        b->SetEnableLighting(false);
        b->SetPosition({ x, y, z });
        b->Update();
        borderBlocks_.push_back(std::move(b));
    };
    for (int x = 0; x <= 28; ++x) { addBlock(static_cast<float>(x), -0.6f, 0.0f); }
    for (int x = 0; x <= 28; ++x) { addBlock(static_cast<float>(x), 13.0f, 0.0f); }
    for (int y = 0; y <= 12; ++y) { addBlock(2.0f,  static_cast<float>(y), 0.0f); }
    for (int y = 0; y <= 12; ++y) { addBlock(28.0f, static_cast<float>(y), 0.0f); }

    for (int i = 0; i < 5; ++i) {
        auto p = std::make_unique<Object3d>();
        p->Initialize(modelCommon_.get());
        p->SetModel(modelBlock_.get());
        p->SetEnableLighting(false);
        p->SetPosition({ kWarpX, 0.4f + static_cast<float>(i) * 1.0f, 0.0f });
        p->SetColor({ 0.1f, 0.9f, 1.0f, 0.9f });
        p->Update();
        warpPortalBlocks_.push_back(std::move(p));
    }

    player_ = std::make_unique<Player>();
    player_->Initialize(modelCommon_.get());

    awakenGaugeBg_ = std::make_unique<Sprite>();
    awakenGaugeBg_->Initialize(spriteCommon_.get(), "Resources/white.png");
    awakenGaugeBg_->SetColor({ 0.05f, 0.05f, 0.15f, 0.75f });

    awakenGaugeFg_ = std::make_unique<Sprite>();
    awakenGaugeFg_->Initialize(spriteCommon_.get(), "Resources/white.png");

    fontRenderer_.Initialize(spriteCommon_.get());

    SSAOEffect::GetInstance()->Initialize(dxCommon_, srvManager_);

    // PBR デモブロック（3 種類を画面中央付近に配置）
    static constexpr float kPBRX[3]        = {  7.0f, 14.0f, 21.0f };
    static constexpr Vector4 kPBRColor[3]  = {
        { 0.80f, 0.55f, 0.45f, 1.0f },  // 非金属（テラコッタ調）
        { 0.90f, 0.90f, 1.00f, 1.0f },  // 鏡面金属（シルバー）
        { 0.55f, 0.65f, 0.70f, 1.0f },  // ラフ金属（ガンメタル）
    };
    for (int i = 0; i < 3; ++i) {
        pbrDemoBlocks_[i] = std::make_unique<Object3d>();
        pbrDemoBlocks_[i]->Initialize(modelCommon_.get());
        pbrDemoBlocks_[i]->SetModel(modelBlock_.get());
        pbrDemoBlocks_[i]->SetPosition({ kPBRX[i], 9.0f, 0.0f });
        pbrDemoBlocks_[i]->SetScale({ 1.5f, 1.5f, 1.5f });
        pbrDemoBlocks_[i]->SetColor(kPBRColor[i]);
        pbrDemoBlocks_[i]->SetMetallic(pbrMetallic_[i]);
        pbrDemoBlocks_[i]->SetRoughness(pbrRoughness_[i]);
        pbrDemoBlocks_[i]->SetShadingTypePBR();
        pbrDemoBlocks_[i]->Update();
    }

    GpuProfiler::GetInstance()->Initialize(dxCommon_);
}

void TrainingScene::Update()
{
    DebugProfiler::GetInstance()->EndFrame();
    DebugProfiler::GetInstance()->BeginFrame();
    fontRenderer_.Reset();

    if (input_->TriggerKey(DIK_BACK)) {
        SceneManager::GetInstance()->ChangeScene("TITLE", 0.4f, 0.4f);
        return;
    }

    // 武器切り替え（Q/E、数字キー 1〜4）
    weaponCycleTimer_ -= GameConstants::kFrameDeltaTime;
    if (weaponCycleTimer_ <= 0.0f) {
        if (input_->TriggerKey(DIK_Q)) { weaponManager_->SelectPrev(); weaponCycleTimer_ = 0.15f; }
        if (input_->TriggerKey(DIK_E)) { weaponManager_->SelectNext(); weaponCycleTimer_ = 0.15f; }
        for (int i = 0; i < weaponManager_->GetCount(); ++i) {
            if (input_->TriggerKey(static_cast<uint8_t>(DIK_1 + i))) {
                weaponManager_->SelectIndex(i);
                weaponCycleTimer_ = 0.15f;
            }
        }
    }

    // 乱舞用ダミーターゲット（正面 8 ユニット先）
    {
        const Vector3& pp = player_->GetPosition();
        player_->Update(input_, { pp.x + player_->GetLastDirX() * 8.0f, pp.y, 0.0f });
    }

    // ── コンボランク追跡 ─────────────────────────────────────────────
    {
        bool hitNow = player_->JustComboHit() || player_->JustFired()
                   || player_->JustSpinShot() || player_->JustRampageHit();
        if (hitNow) {
            trComboCount_++;
            trComboTimer_ = 1.2f;
            trRankAlpha_  = 1.0f;
            if (trComboCount_ > trMaxCombo_) { trMaxCombo_ = trComboCount_; }
        }
        trComboTimer_ -= GameConstants::kFrameDeltaTime;
        if (trComboTimer_ <= 0.0f) {
            trComboTimer_ = 0.0f;
            // コンボ切れ → フェードアウト後にリセット
            trRankAlpha_ -= GameConstants::kFrameDeltaTime * 2.0f;
            if (trRankAlpha_ <= 0.0f) { trRankAlpha_ = 0.0f; trComboCount_ = 0; }
        }
    }

    // カメラ追従
    {
        constexpr float kBlkR = 0.5f;
        const Vector3& pp = player_->GetPosition();
        camera_->SetTranslate({
            std::clamp(pp.x,       2.0f  - kBlkR + GameConstants::kCameraHalfW,  28.0f + kBlkR - GameConstants::kCameraHalfW),
            std::clamp(pp.y + 6.0f, -0.6f - kBlkR + GameConstants::kCameraHalfH,  13.0f + kBlkR - GameConstants::kCameraHalfH),
            -30.0f
        });
    }

    GpuProfiler::GetInstance()->ReadBack();

    shadowManager_->Update(objectCommon_->GetLightDirection());
    Object3d::SetLightViewProjection(shadowManager_->GetLightViewProjection());
    for (auto& b : borderBlocks_) { b->Update(); }

    for (int i = 0; i < 3; ++i) { pbrDemoBlocks_[i]->Update(); }

    warpPulseTimer_ += GameConstants::kFrameDeltaTime;
    float pulse = 0.6f + 0.4f * std::sin(warpPulseTimer_ * 4.0f);
    for (auto& p : warpPortalBlocks_) {
        p->SetColor({ 0.1f * pulse, 0.9f * pulse, 1.0f * pulse, 0.85f });
        p->Update();
    }

    const Vector3& pp  = player_->GetPosition();
    const Vector3& cam = camera_->GetTranslate();
    bool nearWarp = std::abs(pp.x - kWarpX) < kWarpProximity;

    if (nearWarp && input_->TriggerKey(DIK_RETURN)) {
        SceneManager::GetInstance()->ChangeScene("BATTLETEST");
    }

    // ---- UI（FontRenderer） ----
    constexpr float kScale = 1.5f;
    constexpr float kLineH = FontRenderer::kCharH * kScale;
    constexpr Vector4 kColorHeader  = { 1.0f, 0.85f, 0.0f, 1.0f };
    constexpr Vector4 kColorNormal  = { 0.85f, 0.85f, 0.85f, 1.0f };
    constexpr Vector4 kColorSel     = { 1.0f, 1.0f, 0.2f, 1.0f };
    constexpr Vector4 kColorHint    = { 0.6f, 0.6f, 0.6f, 1.0f };

    float px = 12.0f;
    float py = 12.0f;

    fontRenderer_.DrawStringW(L"トレーニングルーム", px, py, kScale, kColorHeader);
    py += kLineH + 2.0f;
    fontRenderer_.DrawStringW(L"-- 武器選択 --", px, py, kScale, kColorNormal);
    py += kLineH + 2.0f;

    const auto& list = weaponManager_->GetList();
    for (int i = 0; i < static_cast<int>(list.size()); ++i) {
        bool sel = (i == weaponManager_->GetIndex());
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s %d.%-8s DMG:%.0f  RNG:%.1f",
            sel ? ">" : " ", i + 1, list[i].name.c_str(), list[i].damage, list[i].range);
        fontRenderer_.DrawString(buf, px, py, kScale, sel ? kColorSel : kColorNormal);
        py += kLineH;
    }

    py += 4.0f;
    fontRenderer_.DrawString("Q/E  1-4 : Switch", px, py, kScale, kColorHint);

    // ワープラベル（ポータルの上）
    if (nearWarp) {
        float sx = (kWarpX - cam.x) / GameConstants::kCameraHalfW * 640.0f + 640.0f;
        float sy = -(5.0f  - cam.y) / GameConstants::kCameraHalfH * 360.0f + 360.0f;
        constexpr Vector4 kColorWarp = { 0.2f, 1.0f, 1.0f, 1.0f };
        fontRenderer_.DrawString("[ ENTER ] Warp", sx - 84.0f, sy - 36.0f, kScale, kColorWarp);
    }

    // ── コンボランク（画面中央） ──────────────────────────────────────
    if (trComboCount_ > 0 || trRankAlpha_ > 0.0f) {
        struct RankDef { const char* label; Vector4 color; };
        static constexpr RankDef kRanks[] = {
            { "D",   { 0.55f, 0.55f, 0.55f, 1.0f } },
            { "C",   { 0.85f, 0.85f, 0.85f, 1.0f } },
            { "B",   { 0.30f, 0.72f, 1.00f, 1.0f } },
            { "A",   { 0.20f, 1.00f, 0.40f, 1.0f } },
            { "S",   { 1.00f, 0.90f, 0.10f, 1.0f } },
            { "SS",  { 1.00f, 0.55f, 0.10f, 1.0f } },
            { "SSS", { 1.00f, 0.30f, 0.30f, 1.0f } },
        };
        int ri = (trComboCount_ >= 25) ? 6 :
                 (trComboCount_ >= 18) ? 5 :
                 (trComboCount_ >= 12) ? 4 :
                 (trComboCount_ >=  8) ? 3 :
                 (trComboCount_ >=  5) ? 2 :
                 (trComboCount_ >=  3) ? 1 : 0;
        const char* lbl = kRanks[ri].label;
        Vector4 rc = kRanks[ri].color;
        rc.w *= trRankAlpha_;

        constexpr float kRS = 5.0f;  // ランク文字スケール
        constexpr float kHS = 2.0f;  // ヒット数スケール
        int   lblLen = static_cast<int>(std::strlen(lbl));
        float rankW  = FontRenderer::kCharW * kRS * static_cast<float>(lblLen);
        fontRenderer_.DrawString(lbl, 640.0f - rankW * 0.5f, 158.0f, kRS, rc);

        char hitBuf[24];
        std::snprintf(hitBuf, sizeof(hitBuf), "x%d HIT", trComboCount_);
        float hw = FontRenderer::kCharW * kHS * static_cast<float>(std::strlen(hitBuf));
        fontRenderer_.DrawString(hitBuf, 640.0f - hw * 0.5f, 240.0f, kHS,
            { 1.0f, 1.0f, 1.0f, trRankAlpha_ });

        if (trMaxCombo_ > 0) {
            char bestBuf[24];
            std::snprintf(bestBuf, sizeof(bestBuf), "BEST:%d", trMaxCombo_);
            constexpr float kBS = 1.3f;
            float bw = FontRenderer::kCharW * kBS * static_cast<float>(std::strlen(bestBuf));
            fontRenderer_.DrawString(bestBuf, 640.0f - bw * 0.5f, 270.0f, kBS,
                { 0.7f, 0.7f, 0.7f, trRankAlpha_ * 0.8f });
        }
    }

    // ── デバッグ情報（右上隅） ──────────────────────────────────────
#ifdef _DEBUG
    {
        char dbgBuf[64];
        float fps = DebugProfiler::GetInstance()->GetFPS();
        float ms  = DebugProfiler::GetInstance()->GetMs();
        std::snprintf(dbgBuf, sizeof(dbgBuf), "%.0f FPS  %.2f ms", fps, ms);
        fontRenderer_.DrawString(dbgBuf, 1140.0f, 4.0f, 1.2f, { 0.6f, 1.0f, 0.6f, 0.85f });
    }
#endif

#ifdef USE_IMGUI
    // ── PBR マテリアルエディタ ────────────────────────────────────────
    ImGui::SetNextWindowSize(ImVec2(260, 165), ImGuiCond_Once);
    ImGui::SetNextWindowPos(ImVec2(10, 400), ImGuiCond_Once);
    if (ImGui::Begin("PBR Material Demo")) {
        static const char* kLabels[3] = { "Plastic (non-metal)", "Mirror Metal", "Rough Metal" };
        for (int i = 0; i < 3; ++i) {
            ImGui::PushID(i);
            if (ImGui::CollapsingHeader(kLabels[i])) {
                ImGui::SliderFloat("Metallic",  &pbrMetallic_[i],  0.0f, 1.0f);
                ImGui::SliderFloat("Roughness", &pbrRoughness_[i], 0.0f, 1.0f);
                pbrDemoBlocks_[i]->SetMetallic(pbrMetallic_[i]);
                pbrDemoBlocks_[i]->SetRoughness(pbrRoughness_[i]);
            }
            ImGui::PopID();
        }
    }
    ImGui::End();

    // ── プロファイラ ───────────────────────────────────────────────────
    GpuProfiler::GetInstance()->DrawImGui();
#endif

    // ── 操作説明（右パネル） ─────────────────────────────────────────
    {
        constexpr float kIx     = 870.0f;
        constexpr float kIS     = 1.3f;
        constexpr float kILineH = FontRenderer::kCharH * kIS + 2.0f;
        constexpr Vector4 kCH   = { 1.0f, 0.85f, 0.0f, 1.0f };
        constexpr Vector4 kCD   = { 0.72f, 0.72f, 0.72f, 1.0f };
        float iy = 12.0f;

        fontRenderer_.DrawStringW(L"-- 操作説明 --", kIx, iy, kIS, kCH);
        iy += kILineH + 2.0f;

                auto row = [&](const char* key, const wchar_t* desc) {
            std::wstring line(key, key + std::strlen(key));
            line += desc;
            fontRenderer_.DrawStringW(line, kIx, iy, kIS, kCD);
            iy += kILineH;
        };
        row("A / D  ", L": 移動");
        row("W      ", L": ジャンプ");
        row("L      ", L": コンボ (x3)");
        row("K      ", L": 射撃");
        row("SPACE  ", L": スピン連射");
        row("(Air)  ", L": スピン+散弾");
        row("Q / E  ", L": 武器切替");
        row("1-4", L": Weapon Select");
        row("ENTER", L": Warp (portal)");
        row("R", L": Awaken (30%+)");
    }

    // ── 覚醒ゲージ UI ────────────────────────────────────────────────
    {
        constexpr float kBarW = 280.0f;
        constexpr float kBarH =  14.0f;
        constexpr float kBarX = 640.0f - kBarW * 0.5f;
        constexpr float kBarY = 700.0f;
        float gauge = player_->GetAwakenGauge();
        bool  awake = player_->IsAwakened();

        awakenGaugeBg_->SetPosition({ kBarX, kBarY });
        awakenGaugeBg_->SetSize({ kBarW, kBarH });
        awakenGaugeBg_->Update();

        float pulse = awake ? (0.7f + 0.3f * std::sin(warpPulseTimer_ * 8.0f)) : 1.0f;
        Vector4 fgColor = awake
            ? Vector4{ 0.05f * pulse, 0.6f * pulse, 1.0f, 0.95f }
            : Vector4{ 0.10f, 0.45f, 0.95f, 0.85f };
        awakenGaugeFg_->SetColor(fgColor);
        awakenGaugeFg_->SetPosition({ kBarX, kBarY });
        awakenGaugeFg_->SetSize({ kBarW * gauge, kBarH });
        awakenGaugeFg_->Update();

        constexpr float kGS = 1.5f;
        constexpr Vector4 kLC = { 0.6f, 0.8f, 1.0f, 0.9f };
        fontRenderer_.DrawString("AWAKEN", kBarX, kBarY - 18.0f, kGS, kLC);
        if (awake) {
            fontRenderer_.DrawString("ACTIVE", kBarX + kBarW - 70.0f, kBarY - 18.0f, kGS,
                { pulse, pulse, 1.0f, 1.0f });
        } else if (gauge >= 0.3f) {
            fontRenderer_.DrawString("[R] Activate", kBarX + kBarW * 0.5f - 70.0f,
                kBarY - 18.0f, kGS, { 0.8f, 0.9f, 1.0f, 0.8f });
        }
    }
}

void TrainingScene::Draw()
{
    ID3D12GraphicsCommandList* cmd = dxCommon_->GetCommandList();
    auto* gpuProfiler = GpuProfiler::GetInstance();

    // ---- シャドウパス ----
    gpuProfiler->BeginScope(GpuProfiler::Shadow, cmd);
    shadowManager_->BeginShadowPass(cmd);
    modelCommon_->BeginShadowPass();
    for (int i = 0; i < 3; ++i) { pbrDemoBlocks_[i]->DrawShadow(); }
    shadowManager_->EndShadowPass(cmd);
    gpuProfiler->EndScope(GpuProfiler::Shadow, cmd);

    // ---- SSAO ノーマルキャプチャパス ----
    auto* ssao = SSAOEffect::GetInstance();
    gpuProfiler->BeginScope(GpuProfiler::SSAO, cmd);
    if (ssao->IsEnabled()) {
        ssao->BeginNormalCapture(dxCommon_, camera_.get());
        for (auto& b : borderBlocks_)     { b->DrawForNormalCapture(); }
        for (auto& p : warpPortalBlocks_) { p->DrawForNormalCapture(); }
        ssao->EndNormalCapture(dxCommon_);
    }
    gpuProfiler->EndScope(GpuProfiler::SSAO, cmd);

    // ---- メイン3D描画 ----
    gpuProfiler->BeginScope(GpuProfiler::Main3D, cmd);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = dxCommon_->GetCurrentBackBufferHandle();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = dxCommon_->GetDsvHandle();
    cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    D3D12_VIEWPORT vp = { 0, 0,
        static_cast<float>(WinApp::kClientWidth), static_cast<float>(WinApp::kClientHeight),
        0.0f, 1.0f };
    D3D12_RECT scissor = { 0, 0, WinApp::kClientWidth, WinApp::kClientHeight };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &scissor);

    modelCommon_->CommonDrawSettings();
    objectCommon_->SetDefaultLight(cmd);
    shadowManager_->SetShadowMap(cmd, srvManager_);

    for (auto& b : borderBlocks_)     { b->Draw(); }
    for (auto& p : warpPortalBlocks_) { p->Draw(); }
    for (int i = 0; i < 3; ++i)       { pbrDemoBlocks_[i]->Draw(); }
    player_->Draw();

    // ---- SSAO 計算 → ブラー → 乗算合成 ----
    if (ssao->IsEnabled()) {
        ssao->Compute(dxCommon_, camera_.get());
        ssao->Blur(dxCommon_);
        cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv); // バックバッファに戻す
        ssao->Apply(dxCommon_, srvManager_);
    }
    gpuProfiler->EndScope(GpuProfiler::Main3D, cmd);

    // ---- GPU タイムスタンプ解決（PostDraw 後の ReadBack で取得） ----
    gpuProfiler->Resolve(cmd);

    // ---- 2D スプライト（テキスト UI） ----
    spriteCommon_->CommonDrawSettings();
    shadowManager_->SetShadowMap(cmd, srvManager_);
    awakenGaugeBg_->Draw();
    if (player_->GetAwakenGauge() > 0.0f) { awakenGaugeFg_->Draw(); }
    fontRenderer_.Draw();
}

void TrainingScene::Finalize()
{
    GpuProfiler::GetInstance()->Finalize();
}
