#pragma once

#include "Assets/AssetTypes.h"
#include "Core/Error.h"

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <fstream>

namespace Limitless::Assets
{
    enum class AssetBundleCompression : uint32_t
    {
        None = 0,
        Zstd = 1
    };

    enum class AssetBundlePayloadFormat : uint32_t
    {
        Raw = 0,
        CookedTexture2D = 1,
        CookedShaderStages = 2
    };

    // -----------------------------------------------------------------------------
    // AssetBundle
    //
    // Runtime asset bundle used for "built game" scenarios where the source `Assets/`
    // folder is not shipped.
    //
    // Format:
    // - Manifest JSON describes entries (guid/key/type/offset/size) and the data file name
    // - Data file is a flat concatenation of raw asset bytes
    //
    // Notes:
    // - This is a packaging layer (raw bytes) rather than a "cooked" format.
    //   It still allows runtime decode/compile (textures, shaders) without shipping source files.
    // - Threading: reads are guarded by a mutex around the shared ifstream.
    // -----------------------------------------------------------------------------
    class AssetBundle final
    {
    public:
        struct Entry
        {
            std::string Guid;
            std::string Key;
            AssetType Type = AssetType::Unknown;

            // How the payload bytes are stored in the bundle.
            AssetBundlePayloadFormat PayloadFormat = AssetBundlePayloadFormat::Raw;
            AssetBundleCompression Compression = AssetBundleCompression::None;

            // Data file location.
            uint64_t Offset = 0;
            uint64_t Size = 0;             // Stored payload size (compressed if Compression != None)
            uint64_t UncompressedSize = 0; // Uncompressed payload size (required for decompression)

            // Build-time content hash used for incremental builds.
            uint64_t ContentHash64 = 0;
        };

        static AssetBundle& GetInstance();

        void Enable(bool enable) { m_Enabled = enable; }
        bool IsEnabled() const { return m_Enabled; }

        bool IsLoaded() const { return m_Loaded; }

        // Load from a manifest path on disk (the data file path is relative to the manifest directory).
        Result<void> LoadFromManifestFile(const std::filesystem::path& manifestPath);

        // Convenience: `<directory>/AssetBundleManifest.json`
        Result<void> LoadFromDirectory(const std::filesystem::path& directory);

        // Convenience: load the bundle produced by the build pipeline:
        // `<ProjectRoot>/Build/AssetBundle/AssetBundleManifest.json`
        Result<void> LoadFromProjectBuildOutput();

        // Convenience: load from packaged layout next to the executable:
        // `<exeDir>/AssetBundle/AssetBundleManifest.json`
        Result<void> LoadFromExecutableDirectory();

        void Unload();

        std::optional<Entry> FindEntryByKey(const std::string& key) const;
        std::optional<Entry> FindEntryByGuid(const std::string& guid) const;

        std::optional<std::string> FindGuidByKey(const std::string& key) const;
        std::optional<std::string> FindKeyByGuid(const std::string& guid) const;

        Result<std::vector<uint8_t>> ReadAllBytesByKey(const std::string& key);
        Result<std::vector<uint8_t>> ReadAllBytesByGuid(const std::string& guid);

        Result<std::string> ReadAllTextByKey(const std::string& key);
        Result<std::string> ReadAllTextByGuid(const std::string& guid);

    private:
        AssetBundle() = default;

        Result<std::vector<uint8_t>> ReadAllBytesByEntryLocked(const Entry& entry);

    private:
        bool m_Enabled = false;
        bool m_Loaded = false;

        std::filesystem::path m_ManifestPath;
        std::filesystem::path m_DataPath;

        // Key -> Entry
        std::unordered_map<std::string, Entry> m_ByKey;
        // Guid -> Key
        std::unordered_map<std::string, std::string> m_KeyByGuid;

        std::mutex m_ReadMutex;
        std::ifstream m_DataStream;
    };
}

