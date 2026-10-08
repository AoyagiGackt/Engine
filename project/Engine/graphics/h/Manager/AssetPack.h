/**
 * @file AssetPack.h
 * @brief 読み込み済みの形へ変換（クック）した素材を1つのpakファイルにまとめ、起動時の変換処理を省くための仕組み
 *
 * 【動作モード】Resources/Config/asset_pack.json の "mode" で切り替える（設定ファイルが無く pak だけある配布時は use）
 *   "auto" : 元素材の追加・変更・削除があればcook、それ以外と提出先ではuse
 *   "off"  : 今まで通り Resources の元ファイルを毎回変換して読む（ビフォー計測用）
 *   "cook" : 元ファイルから読みつつ、変換結果を記録し、終了時に exe と同じフォルダへ pak を書き出す
 *   "use"  : pak に入っている素材は変換済みデータをそのまま使う（入っていない素材は元ファイルから読む）
 *
 * 【pakの中身】キー（"tex:Resources/..." 等）→ 変換済みバイト列
 *   テクスチャ: ミップマップ生成済みのDDS / モデル: 頂点・インデックス配列 / シェーダー: コンパイル済みバイトコード
 *   スキンメッシュ・骨・アニメーション: エンジン内部の構造体をそのまま並べたバイナリ
 *   JSON・音（"raw:Resources/..."）: 変換せず元ファイルのまま（元ファイルが無い時だけ pak から読む）
 */
#pragma once
#include <cstdint>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>
namespace engine {

/** @brief 変換済みデータを詰めるための書き込み口（数値・配列・文字列を順に並べる） */
class BinaryWriter {
public:
    /** @brief 数値や数値だけでできた構造体をそのまま書く */
    template <typename T>
    void Write(const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>, "メモリをそのまま書ける型だけを渡す");
        const auto* p = reinterpret_cast<const uint8_t*>(&value);
        bytes_.insert(bytes_.end(), p, p + sizeof(T));
    }
    /** @brief 要素数を先頭に付けて配列を書く */
    template <typename T>
    void WriteVector(const std::vector<T>& values)
    {
        static_assert(std::is_trivially_copyable_v<T>, "メモリをそのまま書ける型だけを渡す");
        Write(static_cast<uint64_t>(values.size()));
        const auto* p = reinterpret_cast<const uint8_t*>(values.data());
        bytes_.insert(bytes_.end(), p, p + sizeof(T) * values.size());
    }
    /** @brief 長さを先頭に付けて文字列を書く */
    void WriteString(const std::string& text)
    {
        Write(static_cast<uint64_t>(text.size()));
        bytes_.insert(bytes_.end(), text.begin(), text.end());
    }
    std::vector<uint8_t>& Bytes() { return bytes_; }

private:
    std::vector<uint8_t> bytes_;
};

/** @brief BinaryWriterで書いた順に読み戻す読み取り口（壊れたデータは IsValid() が false になる） */
class BinaryReader {
public:
    explicit BinaryReader(const std::vector<uint8_t>& bytes) : bytes_(bytes) { }

    template <typename T>
    T Read()
    {
        static_assert(std::is_trivially_copyable_v<T>, "メモリをそのまま読める型だけを渡す");
        T value {};
        if (!Has(sizeof(T))) {
            return value;
        }
        std::memcpy(&value, bytes_.data() + cursor_, sizeof(T));
        cursor_ += sizeof(T);
        return value;
    }
    template <typename T>
    std::vector<T> ReadVector()
    {
        const uint64_t count = Read<uint64_t>();
        std::vector<T> values;
        if (!Has(static_cast<size_t>(count * sizeof(T)))) {
            return values;
        }
        values.resize(static_cast<size_t>(count));
        std::memcpy(values.data(), bytes_.data() + cursor_, sizeof(T) * values.size());
        cursor_ += sizeof(T) * values.size();
        return values;
    }
    std::string ReadString()
    {
        const uint64_t length = Read<uint64_t>();
        if (!Has(static_cast<size_t>(length))) {
            return {};
        }
        std::string text(reinterpret_cast<const char*>(bytes_.data() + cursor_), static_cast<size_t>(length));
        cursor_ += static_cast<size_t>(length);
        return text;
    }
    /** @brief 途中で足りなくなっていなければtrue（最後まで読めたかの確認にも使う） */
    bool IsValid() const { return valid_; }

private:
    bool Has(size_t size)
    {
        if (!valid_ || cursor_ + size > bytes_.size()) {
            valid_ = false;
            return false;
        }
        return true;
    }

    const std::vector<uint8_t>& bytes_;
    size_t cursor_ = 0;
    bool valid_ = true;
};

/** @brief pakファイルの読み込み（use）と書き出し（cook）を管理するシングルトン */
class AssetPack {
public:
    enum class Mode { Off, Use, Cook };

    static AssetPack* GetInstance();

    /** @brief 設定ファイルを読み、useならpakの目次を開く（最初の素材読み込みより前に呼ぶ） */
    void Initialize(bool forceCook = false, bool prepareAssets = false);
    /** @brief cookなら記録した素材をpakへ書き出す（終了時に呼ぶ） */
    void Finalize();

    Mode GetMode() const { return mode_; }
    bool IsCooking() const { return mode_ == Mode::Cook; }

    /**
     * @brief useモードで、キーに対応する変換済みデータを読む
     * @return pakに入っていればtrue（offやcook、未収録ならfalseで、呼び出し側は元ファイルから読む）
     */
    bool Read(const std::string& key, std::vector<uint8_t>& out);

    /** @brief cookモードで、変換済みデータを記録する（同じキーは最初の1回だけ） */
    void Record(const std::string& key, std::vector<uint8_t> data);

    /** @brief useモードで、pakにキーが入っているか（元ファイルが無くても読めるかの判定用） */
    bool Contains(const std::string& key) const;

    /**
     * @brief 元ファイルが無い時に、pakへそのまま入れたファイル（"raw:"+パス）を読む
     * @return 元ファイルがあるか、pakにも無ければfalse（呼び出し側は今まで通り元ファイルを開く）
     */
    bool ReadRawIfMissing(const std::string& path, std::vector<uint8_t>& out);

private:
    AssetPack() = default;

    struct Entry {
        uint64_t offset = 0;
        uint64_t size = 0;
    };

    /** @brief pakの目次（キー・位置・大きさ）を読み込む。形式が違えばfalse */
    bool OpenPack(const std::wstring& path);
    /** @brief exeと同じフォルダを基準にした pak のパス */
    std::wstring ResolvePackPath(const std::string& fileName) const;
    /** @brief cook終了時に、Resources内のJSONと音声を元ファイルのまま記録する（配布時にResourcesが無くても動くように） */
    void RecordRawFiles();

    Mode mode_ = Mode::Off;
    std::wstring packPath_;
    std::string sourceSnapshot_;
    std::ifstream file_;
    std::unordered_map<std::string, Entry> entries_;
    std::vector<std::pair<std::string, std::vector<uint8_t>>> recorded_;
    std::unordered_map<std::string, size_t> recordedIndex_;
    mutable std::mutex mutex_;
    size_t hitCount_ = 0;
    size_t missCount_ = 0;
};

} // namespace engine
