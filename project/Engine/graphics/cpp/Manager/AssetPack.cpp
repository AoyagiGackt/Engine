/**
 * @file AssetPack.cpp
 * @brief 変換済み素材pakの目次読み込み・データ取得・書き出し（AssetPack）の実装
 */
#include "AssetPack.h"
#include "JsonHelper.h"
#include "Logger.h"
#include "StringUtility.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iterator>
#include <stdexcept>
#define NOMINMAX
#include <windows.h>

namespace engine {
namespace {
constexpr const char* kSettingsPath = "Resources/Config/asset_pack.json";
constexpr const char* kDefaultPackName = "Resources/game.pak";
constexpr const char* kLegacyPackName = "game.pak"; // 以前の3ファイル構成のpak
constexpr const char* kSourceSnapshotKey = "manifest:resources";
constexpr char kMagic[4] = { 'E', 'P', 'A', 'K' };
constexpr uint32_t kVersion = 1;
constexpr double kBytesPerMegabyte = 1024.0 * 1024.0;
constexpr const char* kResourceRoot = "Resources";
constexpr const char* kRawKeyPrefix = "raw:";
// 変換せずそのままpakへ入れるファイルの拡張子（実行時に元ファイルを直接開いて読むもの）
constexpr const char* kRawExtensions[] = { ".json", ".mp3", ".wav" };
// 素材の変更検知から外す拡張子（pak自体・実行時に作られるもの）
constexpr const char* kSnapshotIgnoredExtensions[] = { ".pak", ".dll", ".tmp" };
constexpr const char* kAutoSaveMarker = ".autosave.";

// ファイル先頭: マジック(4) + バージョン(4) + 項目数(8) + 目次の大きさ(8)
constexpr uint64_t kHeaderSize = sizeof(kMagic) + sizeof(uint32_t) + sizeof(uint64_t) + sizeof(uint64_t);
// 目次1件: キー長(8) + キー + データ位置(8) + データの大きさ(8)
constexpr uint64_t kIndexFixedSize = sizeof(uint64_t) * 3;

/** @brief 素材フォルダと実行ファイルの大きさ・更新日時の一覧を作る（pak を作り直すかの判定に使う） */
std::string ResourceSnapshot()
{
    // Resources 以下の素材ファイルを、パス・大きさ・更新時刻の1行ずつにする（自動保存などは除く）
    std::vector<std::string> files;
    for (const auto& item : std::filesystem::recursive_directory_iterator(kResourceRoot))
    {
        if (!item.is_regular_file())
        {
            continue;
        }
        const std::string path = item.path().generic_string();
        std::string extension = item.path().extension().string();
        for (char& c : extension)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        const bool ignored = std::any_of(std::begin(kSnapshotIgnoredExtensions), std::end(kSnapshotIgnoredExtensions),
            [&extension](const char* ignoredExtension) {
                return extension == ignoredExtension;
            });
        if (ignored || path.find(kAutoSaveMarker) != std::string::npos)
        {
            continue;
        }
        files.push_back(path + ":" + std::to_string(item.file_size()) + ":"
            + std::to_string(item.last_write_time().time_since_epoch().count()));
    }
    // 並び順を固定して、同じ中身なら毎回同じ文字列になるようにする
    std::sort(files.begin(), files.end());
    std::string snapshot;
    // 実行ファイルが変わった場合も、バイナリ形式の変化に備えて作り直す。
    wchar_t executable[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, executable, MAX_PATH);
    snapshot += "exe:" + std::to_string(std::filesystem::file_size(executable)) + ":"
        + std::to_string(std::filesystem::last_write_time(executable).time_since_epoch().count()) + "\n";
    for (const auto& file : files)
    {
        snapshot += file + "\n";
    }
    return snapshot;
}

/** @brief 値をそのままのバイト列でファイルへ書く */
template <typename T>
void WriteRaw(std::ofstream& out, const T& value)
{
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

/** @brief ファイルから値をそのままのバイト列で読む（読めなければ false） */
template <typename T>
bool ReadRaw(std::ifstream& in, T& value)
{
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
    return static_cast<bool>(in);
}
} // namespace

/** @brief 共有インスタンスを取得する */
AssetPack* AssetPack::GetInstance()
{
    static AssetPack instance;
    return &instance;
}

/** @brief exeと同じフォルダを基準にした pak のパス */
std::wstring AssetPack::ResolvePackPath(const std::string& fileName) const
{
    // VSから起動しても作業フォルダはprojectになるため、exeの置き場所（generated/Outputs/<構成>）を基準にする
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    const std::filesystem::path directory = std::filesystem::path(exePath).parent_path();
    return (directory / StringUtility::ConvertString(fileName)).wstring();
}

/** @brief 設定ファイルを読み、useならpakの目次を開く（最初の素材読み込みより前に呼ぶ） */
void AssetPack::Initialize(bool forceCook, bool prepareAssets)
{
    const nlohmann::json settings = JsonHelper::Load(kSettingsPath);
    // 提出用フォルダーには元素材が無いので、設定ファイルが無ければ必ずuse。
    std::string mode = forceCook ? "cook" : settings.is_object() ? settings.value("mode", "auto") : "use";
    if (prepareAssets && !forceCook)
    {
        mode = "auto";
    }
    const std::string fileName = settings.is_object() ? settings.value("path", kDefaultPackName) : kDefaultPackName;
    packPath_ = ResolvePackPath(fileName);

    const StartContext context = { std::filesystem::is_directory("Resources/shaders"), fileName };
    if (prepareAssets && !context.hasSources)
    {
        throw std::runtime_error("Asset preparation requires the source Resources folder");
    }

    // 設定ファイルのモード名と起動処理の対応（載っていない名前は off）
    struct ModeStart {
        const char* name;
        void (AssetPack::*start)(const StartContext&);
    };
    static constexpr ModeStart kModeStarts[] = {
        { "auto", &AssetPack::StartAuto },
        { "cook", &AssetPack::StartCook },
        { "use", &AssetPack::StartUse },
        { "off", &AssetPack::StartOff },
    };
    const auto found = std::find_if(std::begin(kModeStarts), std::end(kModeStarts),
        [&mode](const ModeStart& entry) {
            return mode == entry.name;
        });
    (this->*(found != std::end(kModeStarts) ? found->start : &AssetPack::StartOff))(context);
}

/** @brief 元素材に変更が無ければ use、あれば cook で起動する */
void AssetPack::StartAuto(const StartContext& context)
{
    if (!context.hasSources)
    {
        StartUse(context);
        return;
    }
    sourceSnapshot_ = ResourceSnapshot();
    if (OpenPack(packPath_))
    {
        mode_ = Mode::Use;
        std::vector<uint8_t> previous;
        if (Read(kSourceSnapshotKey, previous)
            && std::string(previous.begin(), previous.end()) == sourceSnapshot_)
        {
            Logger::Log("[AssetPack] auto: 素材に変更がないためuseで起動します");
            return;
        }
        file_.close();
        file_.clear();
        entries_.clear();
    }
    StartCook(context);
}

/** @brief 元ファイルから読みつつ変換結果を記録する */
void AssetPack::StartCook(const StartContext& context)
{
    if (!context.hasSources)
    {
        throw std::runtime_error("Asset cook requires the source Resources folder");
    }
    sourceSnapshot_ = ResourceSnapshot();
    mode_ = Mode::Cook;
    Logger::Log("[AssetPack] cook: 読み込んだ素材を変換済みの形で記録し、終了時に "
        + StringUtility::ConvertString(packPath_) + " へ書き出します");
}

/** @brief pak を開いて変換済みデータを使う（開けなければ off） */
void AssetPack::StartUse(const StartContext& context)
{
    bool opened = OpenPack(packPath_);
    // 以前の3ファイル構成も読み込み可能にしておく。
    if (!opened && context.fileName == kDefaultPackName)
    {
        const auto legacyPath = ResolvePackPath(kLegacyPackName);
        opened = OpenPack(legacyPath);
        if (opened)
        {
            packPath_ = legacyPath;
        }
    }
    if (opened)
    {
        mode_ = Mode::Use;
        Logger::Log("[AssetPack] use: " + std::to_string(entries_.size()) + " 件の変換済み素材を "
            + StringUtility::ConvertString(packPath_) + " から読みます");
    }
    else
    {
        mode_ = Mode::Off;
        Logger::LogWarning("[AssetPack] use指定ですがpakを開けませんでした。元ファイルから読みます（先にcookで作成してください）");
    }
}

/** @brief 元ファイルを毎回変換して読む */
void AssetPack::StartOff(const StartContext&)
{
    mode_ = Mode::Off;
    Logger::Log("[AssetPack] off: 元ファイルを毎回変換して読みます");
}

/** @brief pakの目次（キー・位置・大きさ）を読み込む。形式が違えばfalse */
bool AssetPack::OpenPack(const std::wstring& path)
{
    // 前に開いていた pak を閉じてから開き直す
    if (file_.is_open())
    {
        file_.close();
    }
    file_.clear();
    file_.open(path, std::ios::binary);
    if (!file_)
    {
        return false;
    }
    // 先頭の識別子・形式の版・素材数・目次の大きさを確かめる（違えば使わない）
    char magic[sizeof(kMagic)] = {};
    uint32_t version = 0;
    uint64_t count = 0;
    uint64_t indexSize = 0;
    file_.read(magic, sizeof(magic));
    if (!file_ || std::memcmp(magic, kMagic, sizeof(kMagic)) != 0
        || !ReadRaw(file_, version) || version != kVersion
        || !ReadRaw(file_, count) || !ReadRaw(file_, indexSize))
    {
        file_.close();
        return false;
    }
    // 目次を読み、素材名から位置と大きさを引けるようにする
    entries_.clear();
    entries_.reserve(static_cast<size_t>(count));
    for (uint64_t i = 0; i < count; ++i)
    {
        uint64_t keyLength = 0;
        Entry entry;
        if (!ReadRaw(file_, keyLength))
        {
            break;
        }
        std::string key(static_cast<size_t>(keyLength), '\0');
        file_.read(key.data(), static_cast<std::streamsize>(keyLength));
        if (!ReadRaw(file_, entry.offset) || !ReadRaw(file_, entry.size))
        {
            break;
        }
        entries_.emplace(std::move(key), entry);
    }
    // 目次が途中で切れていたら壊れているとみなして使わない
    if (entries_.size() != count)
    {
        entries_.clear();
        file_.close();
        return false;
    }
    return true;
}

/** @brief useモードで、キーに対応する変換済みデータを読む */
bool AssetPack::Read(const std::string& key, std::vector<uint8_t>& out)
{
    if (mode_ != Mode::Use)
    {
        return false;
    }
    std::scoped_lock lock(mutex_);
    const auto it = entries_.find(key);
    if (it == entries_.end())
    {
        // 配布前の確認用に、pakへ入っていなかった素材を名指しで残す（cookで遊んでいない画面の素材など）
        ++missCount_;
        Logger::LogWarning("[AssetPack] pakに無い素材: " + key);
        return false;
    }
    out.resize(static_cast<size_t>(it->second.size));
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(it->second.offset));
    file_.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    if (!file_)
    {
        ++missCount_;
        return false;
    }
    ++hitCount_;
    return true;
}

/** @brief useモードで、pakにキーが入っているか（元ファイルが無くても読めるかの判定用） */
bool AssetPack::Contains(const std::string& key) const
{
    if (mode_ != Mode::Use)
    {
        return false;
    }
    std::scoped_lock lock(mutex_);
    return entries_.count(key) != 0;
}

/** @brief 元ファイルが無い時に、pakへそのまま入れたファイル（"raw:"+パス）を読む */
bool AssetPack::ReadRawIfMissing(const std::string& path, std::vector<uint8_t>& out)
{
    if (mode_ != Mode::Use)
    {
        return false;
    }
    // 開発中は編集した元ファイルを優先し、配布時（元ファイルが無い時）だけpakから読む
    std::error_code error;
    if (std::filesystem::exists(path, error))
    {
        return false;
    }
    return Read(kRawKeyPrefix + path, out);
}

/** @brief cook終了時に、Resources内のJSONと音声を元ファイルのまま記録する（配布時にResourcesが無くても動くように） */
void AssetPack::RecordRawFiles()
{
    std::error_code error;
    for (const auto& item : std::filesystem::recursive_directory_iterator(kResourceRoot, error))
    {
        if (!item.is_regular_file())
        {
            continue;
        }
        std::string extension = item.path().extension().string();
        for (char& c : extension)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        bool isRaw = false;
        for (const char* rawExtension : kRawExtensions)
        {
            isRaw = isRaw || extension == rawExtension;
        }
        // 自動保存の途中データはゲームで使わないので入れない
        const std::string path = item.path().generic_string();
        if (!isRaw || path.find(".autosave.") != std::string::npos)
        {
            continue;
        }
        std::ifstream in(item.path(), std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        Record(kRawKeyPrefix + path, std::move(bytes));
    }
}

/** @brief cookモードで、変換済みデータを記録する（同じキーは最初の1回だけ） */
void AssetPack::Record(const std::string& key, std::vector<uint8_t> data)
{
    if (mode_ != Mode::Cook)
    {
        return;
    }
    std::scoped_lock lock(mutex_);
    if (recordedIndex_.count(key))
    {
        return;
    }
    recordedIndex_.emplace(key, recorded_.size());
    recorded_.emplace_back(key, std::move(data));
}

/** @brief cookなら記録した素材をpakへ書き出す（終了時に呼ぶ） */
void AssetPack::Finalize()
{
    // pak を使っていた時は、pak から読めた数と読めなかった数を記録して閉じる
    if (mode_ == Mode::Use)
    {
        Logger::Log("[AssetPack] pakから読んだ素材: " + std::to_string(hitCount_)
            + " 件 / pakに無く元ファイルから読んだ素材: " + std::to_string(missCount_) + " 件");
        file_.close();
        return;
    }
    if (mode_ != Mode::Cook)
    {
        return;
    }
    // pak を作る時: 元ファイルのまま入れる素材と、作った時の素材の状態も記録に足す
    RecordRawFiles();
    Record(kSourceSnapshotKey, std::vector<uint8_t>(sourceSnapshot_.begin(), sourceSnapshot_.end()));
    if (recorded_.empty())
    {
        return;
    }

    // 目次の大きさを先に求め、各素材の位置を決められるようにする
    uint64_t indexSize = 0;
    for (const auto& [key, data] : recorded_)
    {
        indexSize += kIndexFixedSize + key.size();
    }

    // 書き出し途中で落ちても前回のpakを壊さないよう、一時ファイルに書いてから置き換える
    const std::wstring tempPath = packPath_ + L".tmp";
    std::filesystem::create_directories(std::filesystem::path(packPath_).parent_path());
    {
        std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            Logger::LogError("[AssetPack] pakを書き出せませんでした: " + StringUtility::ConvertString(tempPath));
            throw std::runtime_error("Failed to write asset pack");
        }
        // ヘッダー → 目次（素材名・位置・大きさ） → 素材の中身 の順に書く
        out.write(kMagic, sizeof(kMagic));
        WriteRaw(out, kVersion);
        WriteRaw(out, static_cast<uint64_t>(recorded_.size()));
        WriteRaw(out, indexSize);
        uint64_t offset = kHeaderSize + indexSize;
        for (const auto& [key, data] : recorded_)
        {
            WriteRaw(out, static_cast<uint64_t>(key.size()));
            out.write(key.data(), static_cast<std::streamsize>(key.size()));
            WriteRaw(out, offset);
            WriteRaw(out, static_cast<uint64_t>(data.size()));
            offset += data.size();
        }
        for (const auto& [key, data] : recorded_)
        {
            out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        }
        out.flush();
        if (!out)
        {
            throw std::runtime_error("Failed to finish writing asset pack");
        }
    }
    // 書き終えた一時ファイルで本物の pak を置き換え、大きさを記録する
    std::error_code error;
    if (!MoveFileExW(tempPath.c_str(), packPath_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        throw std::runtime_error("Failed to replace asset pack, Windows error " + std::to_string(GetLastError()));
    }
    const double megabytes = static_cast<double>(std::filesystem::file_size(packPath_, error)) / kBytesPerMegabyte;
    Logger::Log("[AssetPack] " + std::to_string(recorded_.size()) + " 件の素材を書き出しました（"
        + std::to_string(static_cast<int>(megabytes)) + " MB）: " + StringUtility::ConvertString(packPath_));
}

} // namespace engine
