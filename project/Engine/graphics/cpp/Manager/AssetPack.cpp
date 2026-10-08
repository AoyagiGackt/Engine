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
using namespace engine;

namespace {
constexpr const char* kSettingsPath = "Resources/Config/asset_pack.json";
constexpr const char* kDefaultPackName = "Resources/game.pak";
constexpr const char* kSourceSnapshotKey = "manifest:resources";
constexpr char kMagic[4] = { 'E', 'P', 'A', 'K' };
constexpr uint32_t kVersion = 1;
constexpr double kBytesPerMegabyte = 1024.0 * 1024.0;
constexpr const char* kResourceRoot = "Resources";
constexpr const char* kRawKeyPrefix = "raw:";
// 変換せずそのままpakへ入れるファイルの拡張子（実行時に元ファイルを直接開いて読むもの）
constexpr const char* kRawExtensions[] = { ".json", ".mp3", ".wav" };

// ファイル先頭: マジック(4) + バージョン(4) + 項目数(8) + 目次の大きさ(8)
constexpr uint64_t kHeaderSize = sizeof(kMagic) + sizeof(uint32_t) + sizeof(uint64_t) + sizeof(uint64_t);
// 目次1件: キー長(8) + キー + データ位置(8) + データの大きさ(8)
constexpr uint64_t kIndexFixedSize = sizeof(uint64_t) * 3;

std::string ResourceSnapshot()
{
    std::vector<std::string> files;
    for (const auto& item : std::filesystem::recursive_directory_iterator(kResourceRoot)) {
        if (!item.is_regular_file()) { continue; }
        const std::string path = item.path().generic_string();
        std::string extension = item.path().extension().string();
        for (char& c : extension) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
        if (extension == ".pak" || extension == ".dll" || extension == ".tmp"
            || path.find(".autosave.") != std::string::npos) { continue; }
        files.push_back(path + ":" + std::to_string(item.file_size()) + ":"
            + std::to_string(item.last_write_time().time_since_epoch().count()));
    }
    std::sort(files.begin(), files.end());
    std::string snapshot;
    // 実行ファイルが変わった場合も、バイナリ形式の変化に備えて作り直す。
    wchar_t executable[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, executable, MAX_PATH);
    snapshot += "exe:" + std::to_string(std::filesystem::file_size(executable)) + ":"
        + std::to_string(std::filesystem::last_write_time(executable).time_since_epoch().count()) + "\n";
    for (const auto& file : files) { snapshot += file + "\n"; }
    return snapshot;
}

template <typename T>
void WriteRaw(std::ofstream& out, const T& value)
{
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
bool ReadRaw(std::ifstream& in, T& value)
{
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
    return static_cast<bool>(in);
}
} // namespace

AssetPack* AssetPack::GetInstance()
{
    static AssetPack instance;
    return &instance;
}

std::wstring AssetPack::ResolvePackPath(const std::string& fileName) const
{
    // VSから起動しても作業フォルダはprojectになるため、exeの置き場所（generated/Outputs/<構成>）を基準にする
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    const std::filesystem::path directory = std::filesystem::path(exePath).parent_path();
    return (directory / StringUtility::ConvertString(fileName)).wstring();
}

void AssetPack::Initialize(bool forceCook, bool prepareAssets)
{
    const nlohmann::json settings = JsonHelper::Load(kSettingsPath);
    // 提出用フォルダーには元素材が無いので、設定ファイルが無ければ必ずuse。
    std::string mode = forceCook ? "cook" : settings.is_object() ? settings.value("mode", "auto") : "use";
    if (prepareAssets && !forceCook) { mode = "auto"; }
    const std::string fileName = settings.is_object() ? settings.value("path", kDefaultPackName) : kDefaultPackName;
    packPath_ = ResolvePackPath(fileName);

    const bool hasSources = std::filesystem::is_directory("Resources/shaders");
    if (prepareAssets && !hasSources) {
        throw std::runtime_error("Asset preparation requires the source Resources folder");
    }
    if (mode == "auto") {
        if (hasSources) {
            sourceSnapshot_ = ResourceSnapshot();
            if (OpenPack(packPath_)) {
                mode_ = Mode::Use;
                std::vector<uint8_t> previous;
                if (Read(kSourceSnapshotKey, previous)
                    && std::string(previous.begin(), previous.end()) == sourceSnapshot_) {
                    Logger::Log("[AssetPack] auto: 素材に変更がないためuseで起動します");
                    return;
                }
                file_.close();
                file_.clear();
                entries_.clear();
            }
            mode = "cook";
        } else {
            mode = "use";
        }
    }

    if (mode == "cook") {
        if (!hasSources) { throw std::runtime_error("Asset cook requires the source Resources folder"); }
        sourceSnapshot_ = ResourceSnapshot();
        mode_ = Mode::Cook;
        Logger::Log("[AssetPack] cook: 読み込んだ素材を変換済みの形で記録し、終了時に "
            + StringUtility::ConvertString(packPath_) + " へ書き出します");
        return;
    }
    if (mode == "use") {
        bool opened = OpenPack(packPath_);
        // 以前の3ファイル構成も読み込み可能にしておく。
        if (!opened && fileName == kDefaultPackName) {
            const auto legacyPath = ResolvePackPath("game.pak");
            opened = OpenPack(legacyPath);
            if (opened) { packPath_ = legacyPath; }
        }
        if (opened) {
            mode_ = Mode::Use;
            Logger::Log("[AssetPack] use: " + std::to_string(entries_.size()) + " 件の変換済み素材を "
                + StringUtility::ConvertString(packPath_) + " から読みます");
        } else {
            mode_ = Mode::Off;
            Logger::LogWarning("[AssetPack] use指定ですがpakを開けませんでした。元ファイルから読みます（先にcookで作成してください）");
        }
        return;
    }
    mode_ = Mode::Off;
    Logger::Log("[AssetPack] off: 元ファイルを毎回変換して読みます");
}

bool AssetPack::OpenPack(const std::wstring& path)
{
    if (file_.is_open()) { file_.close(); }
    file_.clear();
    file_.open(path, std::ios::binary);
    if (!file_) {
        return false;
    }
    char magic[sizeof(kMagic)] = {};
    uint32_t version = 0;
    uint64_t count = 0;
    uint64_t indexSize = 0;
    file_.read(magic, sizeof(magic));
    if (!file_ || std::memcmp(magic, kMagic, sizeof(kMagic)) != 0
        || !ReadRaw(file_, version) || version != kVersion
        || !ReadRaw(file_, count) || !ReadRaw(file_, indexSize)) {
        file_.close();
        return false;
    }
    entries_.clear();
    entries_.reserve(static_cast<size_t>(count));
    for (uint64_t i = 0; i < count; ++i) {
        uint64_t keyLength = 0;
        Entry entry;
        if (!ReadRaw(file_, keyLength)) {
            break;
        }
        std::string key(static_cast<size_t>(keyLength), '\0');
        file_.read(key.data(), static_cast<std::streamsize>(keyLength));
        if (!ReadRaw(file_, entry.offset) || !ReadRaw(file_, entry.size)) {
            break;
        }
        entries_.emplace(std::move(key), entry);
    }
    if (entries_.size() != count) {
        entries_.clear();
        file_.close();
        return false;
    }
    return true;
}

bool AssetPack::Read(const std::string& key, std::vector<uint8_t>& out)
{
    if (mode_ != Mode::Use) {
        return false;
    }
    std::scoped_lock lock(mutex_);
    const auto it = entries_.find(key);
    if (it == entries_.end()) {
        // 配布前の確認用に、pakへ入っていなかった素材を名指しで残す（cookで遊んでいない画面の素材など）
        ++missCount_;
        Logger::LogWarning("[AssetPack] pakに無い素材: " + key);
        return false;
    }
    out.resize(static_cast<size_t>(it->second.size));
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(it->second.offset));
    file_.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    if (!file_) {
        ++missCount_;
        return false;
    }
    ++hitCount_;
    return true;
}

bool AssetPack::Contains(const std::string& key) const
{
    if (mode_ != Mode::Use) {
        return false;
    }
    std::scoped_lock lock(mutex_);
    return entries_.count(key) != 0;
}

bool AssetPack::ReadRawIfMissing(const std::string& path, std::vector<uint8_t>& out)
{
    if (mode_ != Mode::Use) {
        return false;
    }
    // 開発中は編集した元ファイルを優先し、配布時（元ファイルが無い時）だけpakから読む
    std::error_code error;
    if (std::filesystem::exists(path, error)) {
        return false;
    }
    return Read(kRawKeyPrefix + path, out);
}

void AssetPack::RecordRawFiles()
{
    std::error_code error;
    for (const auto& item : std::filesystem::recursive_directory_iterator(kResourceRoot, error)) {
        if (!item.is_regular_file()) {
            continue;
        }
        std::string extension = item.path().extension().string();
        for (char& c : extension) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        bool isRaw = false;
        for (const char* rawExtension : kRawExtensions) {
            isRaw = isRaw || extension == rawExtension;
        }
        // 自動保存の途中データはゲームで使わないので入れない
        const std::string path = item.path().generic_string();
        if (!isRaw || path.find(".autosave.") != std::string::npos) {
            continue;
        }
        std::ifstream in(item.path(), std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        Record(kRawKeyPrefix + path, std::move(bytes));
    }
}

void AssetPack::Record(const std::string& key, std::vector<uint8_t> data)
{
    if (mode_ != Mode::Cook) {
        return;
    }
    std::scoped_lock lock(mutex_);
    if (recordedIndex_.count(key)) {
        return;
    }
    recordedIndex_.emplace(key, recorded_.size());
    recorded_.emplace_back(key, std::move(data));
}

void AssetPack::Finalize()
{
    if (mode_ == Mode::Use) {
        Logger::Log("[AssetPack] pakから読んだ素材: " + std::to_string(hitCount_)
            + " 件 / pakに無く元ファイルから読んだ素材: " + std::to_string(missCount_) + " 件");
        file_.close();
        return;
    }
    if (mode_ != Mode::Cook) {
        return;
    }
    RecordRawFiles();
    Record(kSourceSnapshotKey, std::vector<uint8_t>(sourceSnapshot_.begin(), sourceSnapshot_.end()));
    if (recorded_.empty()) {
        return;
    }

    uint64_t indexSize = 0;
    for (const auto& [key, data] : recorded_) {
        indexSize += kIndexFixedSize + key.size();
    }

    // 書き出し途中で落ちても前回のpakを壊さないよう、一時ファイルに書いてから置き換える
    const std::wstring tempPath = packPath_ + L".tmp";
    std::filesystem::create_directories(std::filesystem::path(packPath_).parent_path());
    {
        std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            Logger::LogError("[AssetPack] pakを書き出せませんでした: " + StringUtility::ConvertString(tempPath));
            throw std::runtime_error("Failed to write asset pack");
        }
        out.write(kMagic, sizeof(kMagic));
        WriteRaw(out, kVersion);
        WriteRaw(out, static_cast<uint64_t>(recorded_.size()));
        WriteRaw(out, indexSize);
        uint64_t offset = kHeaderSize + indexSize;
        for (const auto& [key, data] : recorded_) {
            WriteRaw(out, static_cast<uint64_t>(key.size()));
            out.write(key.data(), static_cast<std::streamsize>(key.size()));
            WriteRaw(out, offset);
            WriteRaw(out, static_cast<uint64_t>(data.size()));
            offset += data.size();
        }
        for (const auto& [key, data] : recorded_) {
            out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        }
        out.flush();
        if (!out) { throw std::runtime_error("Failed to finish writing asset pack"); }
    }
    std::error_code error;
    if (!MoveFileExW(tempPath.c_str(), packPath_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        throw std::runtime_error("Failed to replace asset pack, Windows error " + std::to_string(GetLastError()));
    }
    const double megabytes = static_cast<double>(std::filesystem::file_size(packPath_, error)) / kBytesPerMegabyte;
    Logger::Log("[AssetPack] " + std::to_string(recorded_.size()) + " 件の素材を書き出しました（"
        + std::to_string(static_cast<int>(megabytes)) + " MB）: " + StringUtility::ConvertString(packPath_));
}
