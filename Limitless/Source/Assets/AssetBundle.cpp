#include "Assets/AssetBundle.h"

#include "Assets/AssetPaths.h"
#include "Core/Debug/Log.h"
#include "Core/Compression/ZstdCompression.h"
#include "Platform/Platform.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <fstream>

namespace Limitless::Assets
{
    using json = nlohmann::json;

    namespace
    {
        static std::atomic<bool> s_LoggedZstdDecompressionOnce{ false };

        AssetBundleCompression AssetBundleCompressionFromString(const std::string& value)
        {
            if (value == "Zstd")
            {
                return AssetBundleCompression::Zstd;
            }
            return AssetBundleCompression::None;
        }

        AssetBundlePayloadFormat AssetBundlePayloadFormatFromString(const std::string& value)
        {
            if (value == "CookedTexture2D")
            {
                return AssetBundlePayloadFormat::CookedTexture2D;
            }
            if (value == "CookedShaderStages")
            {
                return AssetBundlePayloadFormat::CookedShaderStages;
            }
            return AssetBundlePayloadFormat::Raw;
        }

        Result<std::vector<uint8_t>> DecompressZstd(const std::vector<uint8_t>& storedBytes, const uint64_t uncompressedSize)
        {
            if (!s_LoggedZstdDecompressionOnce.exchange(true))
            {
                LT_CORE_INFO("AssetBundle: using Zstd-compressed payloads (runtime decompression enabled)");
            }

            // Signature is kept local to keep the public AssetBundle API independent of compression libraries.
            return ::Limitless::Compression::ZstdCompression::Decompress(
                storedBytes.data(),
                storedBytes.size(),
                static_cast<size_t>(uncompressedSize));
        }
    }

    AssetBundle& AssetBundle::GetInstance()
    {
        static AssetBundle s_Instance;
        return s_Instance;
    }

    void AssetBundle::Unload()
    {
        std::lock_guard<std::mutex> lock(m_ReadMutex);
        m_ByKey.clear();
        m_KeyByGuid.clear();
        m_Loaded = false;
        m_ManifestPath.clear();
        m_DataPath.clear();
        if (m_DataStream.is_open())
        {
            m_DataStream.close();
        }
    }

    Result<void> AssetBundle::LoadFromManifestFile(const std::filesystem::path& manifestPath)
    {
        Unload();

        if (manifestPath.empty())
        {
            return Result<void>(ErrorCode::InvalidArgument, "AssetBundle: manifestPath is empty");
        }

        if (!std::filesystem::exists(manifestPath))
        {
            return Result<void>(ErrorCode::FileNotFound, "AssetBundle: manifest not found: " + manifestPath.string());
        }

        json root;
        try
        {
            std::ifstream in(manifestPath, std::ios::in | std::ios::binary);
            if (!in.is_open())
            {
                return Result<void>(ErrorCode::FileAccessDenied, "AssetBundle: failed to open manifest: " + manifestPath.string());
            }
            in >> root;
        }
        catch (const std::exception& e)
        {
            return Result<void>(ErrorCode::FileCorrupted, std::string("AssetBundle: manifest parse failed: ") + e.what());
        }

        if (!root.contains("dataFile") || !root["dataFile"].is_string())
        {
            return Result<void>(ErrorCode::FileCorrupted, "AssetBundle: manifest missing 'dataFile'");
        }
        if (!root.contains("entries") || !root["entries"].is_array())
        {
            return Result<void>(ErrorCode::FileCorrupted, "AssetBundle: manifest missing 'entries' array");
        }

        const uint32_t manifestVersion = root.value("version", 1u);

        const std::filesystem::path dataFile = root["dataFile"].get<std::string>();
        const std::filesystem::path baseDir = manifestPath.parent_path();
        const std::filesystem::path dataPath = baseDir / dataFile;

        if (!std::filesystem::exists(dataPath))
        {
            return Result<void>(ErrorCode::FileNotFound, "AssetBundle: data file not found: " + dataPath.string());
        }

        std::unordered_map<std::string, Entry> byKey;
        std::unordered_map<std::string, std::string> keyByGuid;

        for (const auto& e : root["entries"])
        {
            if (!e.is_object())
            {
                continue;
            }

            Entry entry;
            if (e.contains("guid") && e["guid"].is_string()) entry.Guid = e["guid"].get<std::string>();
            if (e.contains("key") && e["key"].is_string()) entry.Key = e["key"].get<std::string>();
            if (e.contains("type") && e["type"].is_string()) entry.Type = AssetTypeFromString(e["type"].get<std::string>());
            if (e.contains("offset") && e["offset"].is_number_unsigned()) entry.Offset = e["offset"].get<uint64_t>();
            if (e.contains("size") && e["size"].is_number_unsigned()) entry.Size = e["size"].get<uint64_t>();

            if (manifestVersion >= 2)
            {
                if (e.contains("payloadFormat") && e["payloadFormat"].is_string()) entry.PayloadFormat = AssetBundlePayloadFormatFromString(e["payloadFormat"].get<std::string>());
                if (e.contains("compression") && e["compression"].is_string()) entry.Compression = AssetBundleCompressionFromString(e["compression"].get<std::string>());
                if (e.contains("uncompressedSize") && e["uncompressedSize"].is_number_unsigned()) entry.UncompressedSize = e["uncompressedSize"].get<uint64_t>();
                if (e.contains("contentHash64") && e["contentHash64"].is_number_unsigned()) entry.ContentHash64 = e["contentHash64"].get<uint64_t>();
            }
            else
            {
                entry.PayloadFormat = AssetBundlePayloadFormat::Raw;
                entry.Compression = AssetBundleCompression::None;
                entry.UncompressedSize = entry.Size;
                entry.ContentHash64 = 0;
            }

            if (entry.UncompressedSize == 0)
            {
                entry.UncompressedSize = entry.Size;
            }

            if (entry.Guid.empty() || entry.Key.empty() || entry.Size == 0)
            {
                continue;
            }

            byKey[entry.Key] = entry;
            keyByGuid[entry.Guid] = entry.Key;
        }

        if (byKey.empty())
        {
            return Result<void>(ErrorCode::FileCorrupted, "AssetBundle: manifest had no valid entries");
        }

        {
            std::lock_guard<std::mutex> lock(m_ReadMutex);
            m_ManifestPath = manifestPath;
            m_DataPath = dataPath;
            m_ByKey = std::move(byKey);
            m_KeyByGuid = std::move(keyByGuid);

            m_DataStream = std::ifstream(m_DataPath, std::ios::in | std::ios::binary);
            if (!m_DataStream.is_open())
            {
                return Result<void>(ErrorCode::FileAccessDenied, "AssetBundle: failed to open data file: " + m_DataPath.string());
            }
        }

        m_Loaded = true;
        LT_CORE_INFO("AssetBundle: loaded {} entries from '{}'", m_ByKey.size(), m_ManifestPath.string());
        return Result<void>();
    }

    Result<void> AssetBundle::LoadFromProjectBuildOutput()
    {
        const auto rootResult = FindProjectRootFromWorkingDirectory();
        if (rootResult.IsFailure())
        {
            return Result<void>(rootResult.GetError());
        }

        const std::filesystem::path root = rootResult.GetValue();
        const std::filesystem::path manifest = root / "Build" / "AssetBundle" / "AssetBundleManifest.json";
        return LoadFromManifestFile(manifest);
    }

    Result<void> AssetBundle::LoadFromDirectory(const std::filesystem::path& directory)
    {
        if (directory.empty())
        {
            return Result<void>(ErrorCode::InvalidArgument, "AssetBundle: directory is empty");
        }
        return LoadFromManifestFile(directory / "AssetBundleManifest.json");
    }

    Result<void> AssetBundle::LoadFromExecutableDirectory()
    {
        const std::filesystem::path exePath = PlatformDetection::GetExecutablePath();
        if (exePath.empty())
        {
            return Result<void>(ErrorCode::InvalidState, "AssetBundle: executable path is empty");
        }

        const std::filesystem::path exeDir = exePath.has_parent_path() ? exePath.parent_path() : std::filesystem::path();
        if (exeDir.empty())
        {
            return Result<void>(ErrorCode::InvalidState, "AssetBundle: executable directory not found");
        }

        return LoadFromManifestFile(exeDir / "AssetBundle" / "AssetBundleManifest.json");
    }

    std::optional<AssetBundle::Entry> AssetBundle::FindEntryByKey(const std::string& key) const
    {
        const auto it = m_ByKey.find(key);
        if (it == m_ByKey.end())
        {
            return std::nullopt;
        }
        return it->second;
    }

    std::optional<AssetBundle::Entry> AssetBundle::FindEntryByGuid(const std::string& guid) const
    {
        auto key = FindKeyByGuid(guid);
        if (!key.has_value())
        {
            return std::nullopt;
        }
        return FindEntryByKey(*key);
    }

    std::optional<std::string> AssetBundle::FindGuidByKey(const std::string& key) const
    {
        auto entry = FindEntryByKey(key);
        if (!entry.has_value())
        {
            return std::nullopt;
        }
        return entry->Guid;
    }

    std::optional<std::string> AssetBundle::FindKeyByGuid(const std::string& guid) const
    {
        const auto it = m_KeyByGuid.find(guid);
        if (it == m_KeyByGuid.end())
        {
            return std::nullopt;
        }
        return it->second;
    }

    Result<std::vector<uint8_t>> AssetBundle::ReadAllBytesByEntryLocked(const Entry& entry)
    {
        if (!m_DataStream.is_open())
        {
            return Result<std::vector<uint8_t>>(ErrorCode::InvalidState, "AssetBundle: data stream is not open");
        }

        if (entry.Size == 0)
        {
            return Result<std::vector<uint8_t>>(ErrorCode::InvalidArgument, "AssetBundle: entry size is zero");
        }

        std::vector<uint8_t> storedBytes;
        storedBytes.resize(static_cast<size_t>(entry.Size));

        m_DataStream.clear();
        m_DataStream.seekg(static_cast<std::streamoff>(entry.Offset), std::ios::beg);
        if (!m_DataStream.good())
        {
            return Result<std::vector<uint8_t>>(ErrorCode::FileCorrupted, "AssetBundle: seek failed");
        }

        m_DataStream.read(reinterpret_cast<char*>(storedBytes.data()), static_cast<std::streamsize>(storedBytes.size()));
        if (!m_DataStream.good())
        {
            return Result<std::vector<uint8_t>>(ErrorCode::FileCorrupted, "AssetBundle: read failed");
        }

        if (entry.Compression == AssetBundleCompression::None)
        {
            return storedBytes;
        }

        if (entry.Compression == AssetBundleCompression::Zstd)
        {
            return DecompressZstd(storedBytes, entry.UncompressedSize);
        }

        return Result<std::vector<uint8_t>>(ErrorCode::NotSupported, "AssetBundle: unsupported compression");
    }

    Result<std::vector<uint8_t>> AssetBundle::ReadAllBytesByKey(const std::string& key)
    {
        if (!m_Enabled || !m_Loaded)
        {
            return Result<std::vector<uint8_t>>(ErrorCode::InvalidState, "AssetBundle is not enabled/loaded");
        }

        auto entry = FindEntryByKey(key);
        if (!entry.has_value())
        {
            return Result<std::vector<uint8_t>>(ErrorCode::ResourceNotFound, "AssetBundle: key not found: " + key);
        }

        std::lock_guard<std::mutex> lock(m_ReadMutex);
        return ReadAllBytesByEntryLocked(*entry);
    }

    Result<std::vector<uint8_t>> AssetBundle::ReadAllBytesByGuid(const std::string& guid)
    {
        if (!m_Enabled || !m_Loaded)
        {
            return Result<std::vector<uint8_t>>(ErrorCode::InvalidState, "AssetBundle is not enabled/loaded");
        }

        auto entry = FindEntryByGuid(guid);
        if (!entry.has_value())
        {
            return Result<std::vector<uint8_t>>(ErrorCode::ResourceNotFound, "AssetBundle: guid not found");
        }

        std::lock_guard<std::mutex> lock(m_ReadMutex);
        return ReadAllBytesByEntryLocked(*entry);
    }

    Result<std::string> AssetBundle::ReadAllTextByKey(const std::string& key)
    {
        const auto bytes = ReadAllBytesByKey(key);
        if (bytes.IsFailure())
        {
            return Result<std::string>(bytes.GetError());
        }
        const auto& b = bytes.GetValue();
        return std::string(reinterpret_cast<const char*>(b.data()), b.size());
    }

    Result<std::string> AssetBundle::ReadAllTextByGuid(const std::string& guid)
    {
        const auto bytes = ReadAllBytesByGuid(guid);
        if (bytes.IsFailure())
        {
            return Result<std::string>(bytes.GetError());
        }
        const auto& b = bytes.GetValue();
        return std::string(reinterpret_cast<const char*>(b.data()), b.size());
    }
}

