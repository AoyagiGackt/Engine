/**
 * @file UILayout.h
 * @brief 画面UIの位置・サイズ・色をJSONから読み、ステージエディタ上で調整・保存できるようにする仕組み
 *
 * 【使い方】
 *   auto& layout = UILayout::Get("title"); // Resources/Config/UI/title.json
 *   const Vector2 pos = layout.Pos("menu.pos", kMenuPos); // kMenuPosはJSONに無い時の既定値
 *   const Vector2 size = layout.Vec2("menu.size", kMenuSize);
 *
 * 毎フレーム描画時に値を取りに行けば、エディタで動かした結果がその場で反映される。
 * 初めて参照したキーはその時点で登録され、F2のステージエディタ表示中に出る
 * 「UIレイアウト」パネルへ自動で並ぶ。キーの "." より前はパネル上のグループ名になる。
 * Pos()で取った値は画面上にも印が出て、マウスでドラッグして動かせる。
 */
#pragma once
#include "Vector2.h"
#include "Vector4.h"
#include "json.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
namespace engine::game {
using engine::Vector2;
using engine::Vector4;

/** @brief 1画面（または共有HUD）ぶんのUI調整値の集まり */
class UILayout {
public:
    /** @brief 画面上でドラッグできる位置項目1つ（エディタが印の描画と掴み判定に使う） */
    struct PositionHandle {
        UILayout* layout = nullptr;
        size_t index = 0;
        std::string label;
        Vector2 position {};
    };

    /**
     * @brief 名前ごとの唯一のインスタンスを取得する（初回は Resources/Config/UI/<name>.json を読む）
     * @param name レイアウト名（シーン名やHUD名）
     */
    static UILayout& Get(const std::string& name);

    /** @brief 編集パネルに並べる対象をリセットする（シーン切り替え時にSceneManagerが呼ぶ） */
    static void ResetUsedLayouts();

    /** @brief 現在のシーンで参照されたレイアウトの編集パネルを描く（ImGuiのUpdateフェーズで呼ぶ） */
    static void DrawEditorPanel();

    /** @brief 現在のシーンで参照された位置項目をすべて集める */
    static std::vector<PositionHandle> CollectPositionHandles();

    /** @brief 画面上で選択中の位置項目を設定する（パネル上で強調表示する） */
    static void SetSelectedHandle(const UILayout* layout, size_t index);

    /** @brief 画面上で選択中の位置項目か */
    static bool IsSelectedHandle(const UILayout* layout, size_t index);

    /** @brief 数値を取得する（未登録なら既定値で登録する） */
    float Float(const std::string& key, float defaultValue);
    /** @brief 2次元の値（サイズ・間隔など）を取得する（未登録なら既定値で登録する） */
    Vector2 Vec2(const std::string& key, const Vector2& defaultValue);
    /** @brief 画面上の位置を取得する。エディタ表示中は印が出てドラッグで動かせる */
    Vector2 Pos(const std::string& key, const Vector2& defaultValue);
    /** @brief 色を取得する（未登録なら既定値で登録する） */
    Vector4 Color(const std::string& key, const Vector4& defaultValue);

    /** @brief 位置項目をエディタから書き換える */
    void SetPosition(size_t index, const Vector2& position);


    /** @brief 既定値と違う項目だけをJSONへ書き出す */
    void Save();
    /** @brief JSONを読み直して登録済みの項目へ反映する */
    void Reload();

private:
    enum class Kind { Float, Vec2, Pos, Color };

    /** @brief 1項目ぶんの値（要素数は種類による。Floatは1、Vec2/Posは2、Colorは4） */
    struct Entry {
        std::string key;
        Kind kind = Kind::Float;
        float value[4] = {};
        float defaultValue[4] = {};
    };

    explicit UILayout(const std::string& name);

    /** @brief 種類ごとの要素数（Floatは1、Vec2/Posは2、Colorは4） */
    static int ComponentsOf(Kind kind);

    /** @brief キーに対応する項目を探し、無ければ既定値とJSONの値で登録する */
    Entry& FindOrRegister(const std::string& key, Kind kind, const float* defaultValue);
    /** @brief 読み込み済みJSONの値を項目へ反映する（形が合わなければ既定値のまま） */
    void ApplyLoadedValue(Entry& entry) const;
    /** @brief 値が変わったことを記録する（未保存表示と再配置判定用の番号を進める） */
    void MarkChanged();
    /** @brief パネル内に自分の項目を並べる */
    void DrawEntries();

    std::string name_;
    std::string path_;
    nlohmann::json loaded_;
    std::vector<Entry> entries_;
    std::unordered_map<std::string, size_t> indexOf_;
    bool unsaved_ = false;
    bool usedInScene_ = false;
};

} // namespace engine::game
