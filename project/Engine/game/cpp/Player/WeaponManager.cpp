#include "WeaponManager.h"
#include <algorithm>
using namespace engine;
using namespace engine::game;

WeaponManager* WeaponManager::instance_ = nullptr;

WeaponManager* WeaponManager::GetInstance() {
    if (!instance_) { instance_ = new WeaponManager(); }
    return instance_;
}

WeaponManager::WeaponManager() {
    // 初期射撃武器（全スタイル共通）
    ranged_ = {
        "Pistol", 12.0f, 15.0f, 0.18f, "標準的な拳銃",
        {
            { "F",       L"射撃" },
            { "Shift+F", L"連射" },
        }
    };

    // 4スタイル（1〜4キー対応）
    weapons_ = {
        // ── Style 1: 鬼神 (Swordmaster) ──────────────────────────────
        {
            "Sword", "鬼神 (Swordmaster)", L"鬼神", WeaponType::Sword,
            30.0f, 2.5f, 0.50f, "バランス型の全能剣士",
            { 0.95f, 0.35f, 0.15f, 1.0f }, 1.0f,  // 橙赤
            {
                { "L",    L"3段コンボ" },
                { "K",    L"射撃" },
                { "W/Up", L"ジャンプ" },
                { "R",    L"覚醒" },
            }
        },
        // ── Style 2: 銃士 (Gunslinger) ───────────────────────────────
        {
            "Spear", "銃士 (Gunslinger)", L"銃士", WeaponType::Spear,
            20.0f, 4.0f, 0.70f, "遠距離射撃スタイル",
            { 0.20f, 0.65f, 1.0f, 1.0f }, 0.7f,   // シアン青
            {
                { "K",    L"射撃" },
                { "L",    L"格闘" },
                { "W/Up", L"ジャンプ" },
                { "R",    L"覚醒" },
            }
        },
        // ── Style 3: 奇術師 (Trickster) ──────────────────────────────
        {
            "Dagger", "奇術師 (Trickster)", L"奇術師", WeaponType::Dagger,
            15.0f, 1.5f, 0.25f, "高速機動スタイル",
            { 0.20f, 1.0f, 0.45f, 1.0f }, 0.6f,   // ライムグリーン
            {
                { "Space", L"ブリンク" },
                { "K",     L"射撃" },
                { "L",     L"格闘" },
                { "W/Up",  L"ジャンプ" },
                { "R",     L"覚醒" },
            }
        },
        // ── Style 4: 守護者 (Royal Guard) ────────────────────────────
        {
            "Hammer", "守護者 (Royal Guard)", L"守護者", WeaponType::Hammer,
            60.0f, 1.8f, 1.20f, "カウンター防御スタイル",
            { 0.75f, 0.30f, 1.0f, 1.0f }, 2.0f,   // 紫
            {
                { "Space", L"ゲージチャージ" },
                { "K",     L"射撃" },
                { "L",     L"格闘" },
                { "W/Up",  L"ジャンプ" },
                { "R",     L"覚醒" },
            }
        },
        // ── Style 5: 玉術師 (Ball Master) ────────────────────────────
        {
            "Ball", "玉術師 (BallMaster)", L"玉術師", WeaponType::Ball,
            12.0f, 3.0f, 0.30f, "玉を連射するスタイル",
            { 1.0f, 0.45f, 0.85f, 1.0f }, 0.4f,   // ピンク
            {
                { "Space",     L"スピン連射" },
                { "Space(Air)", L"スピン+ばらまき" },
                { "L",         L"格闘" },
                { "K",         L"射撃" },
                { "W/Up",      L"ジャンプ" },
                { "R",         L"覚醒" },
            }
        },
    };
}

void WeaponManager::SelectIndex(int i) {
    index_ = std::clamp(i, 0, static_cast<int>(weapons_.size()) - 1);
}

void WeaponManager::SelectNext() {
    SelectIndex((index_ + 1) % static_cast<int>(weapons_.size()));
}

void WeaponManager::SelectPrev() {
    int n = static_cast<int>(weapons_.size());
    SelectIndex((index_ - 1 + n) % n);
}
