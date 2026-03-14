#pragma once

#include "Assets/AssetTypes.h"
#include "Core/Error.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Limitless::Assets
{
    struct AssetRegistryCacheEntry final
    {
        std::string Guid;
        std::string Key;
        std::string ResolvedPath;
        AssetType Type = AssetType::Unknown;
        std::string ImporterSettingsJson;
        std::vector<std::string> Dependencies;
        uint64_t SourceSizeBytes = 0;
        int64_t SourceLastWriteTimeTicks = 0;
        uint64_t ImporterSettingsHash64 = 0;
        uint32_t ImporterVersion = 1;
    };

    struct AssetRegistryCacheSnapshot final
    {
        uint32_t DatabaseJsonVersion = 0;
        uint64_t SourceSizeBytes = 0;
        int64_t SourceLastWriteTimeTicks = 0;
        std::vector<AssetRegistryCacheEntry> Entries;
    };

    class AssetRegistryCache final
    {
    public:
        static std::filesystem::path GetCacheFilePath(const std::filesystem::path& databaseFilePath);

        static Result<AssetRegistryCacheSnapshot> LoadFromFile(const std::filesystem::path& cacheFilePath,
                                                               uint32_t expectedDatabaseJsonVersion,
                                                               uint64_t expectedSourceSizeBytes,
                                                               int64_t expectedSourceLastWriteTimeTicks);

        static Result<void> SaveToFile(const std::filesystem::path& cacheFilePath,
                                       const AssetRegistryCacheSnapshot& snapshot);
    };
}
