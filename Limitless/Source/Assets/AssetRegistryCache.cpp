#include "Assets/AssetRegistryCache.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <type_traits>

namespace Limitless::Assets
{
    namespace
    {
        constexpr std::array<char, 4> kMagic{{'L', 'T', 'A', 'R'}};
        constexpr uint32_t kSchemaVersion = 2;

        template<typename TValue>
        Result<void> WriteValue(std::ostream& stream, const TValue& value)
        {
            static_assert(std::is_trivially_copyable_v<TValue>);
            stream.write(reinterpret_cast<const char*>(&value), sizeof(TValue));
            if (!stream.good())
            {
                return Result<void>(ErrorCode::FileAccessDenied, "AssetRegistryCache: failed to write binary value");
            }
            return Result<void>();
        }

        template<typename TValue>
        Result<void> ReadValue(std::istream& stream, TValue& value)
        {
            static_assert(std::is_trivially_copyable_v<TValue>);
            stream.read(reinterpret_cast<char*>(&value), sizeof(TValue));
            if (!stream.good())
            {
                return Result<void>(ErrorCode::FileCorrupted, "AssetRegistryCache: failed to read binary value");
            }
            return Result<void>();
        }

        Result<void> WriteString(std::ostream& stream, const std::string& value)
        {
            if (value.size() > std::numeric_limits<uint32_t>::max())
            {
                return Result<void>(ErrorCode::FileTooLarge, "AssetRegistryCache: string is too large to serialize");
            }

            const uint32_t size = static_cast<uint32_t>(value.size());
            auto writeSize = WriteValue(stream, size);
            if (writeSize.IsFailure())
            {
                return writeSize;
            }

            if (size > 0)
            {
                stream.write(value.data(), static_cast<std::streamsize>(size));
                if (!stream.good())
                {
                    return Result<void>(ErrorCode::FileAccessDenied, "AssetRegistryCache: failed to write string payload");
                }
            }

            return Result<void>();
        }

        Result<void> ReadString(std::istream& stream, std::string& value)
        {
            uint32_t size = 0;
            auto readSize = ReadValue(stream, size);
            if (readSize.IsFailure())
            {
                return readSize;
            }

            value.resize(size);
            if (size > 0)
            {
                stream.read(value.data(), static_cast<std::streamsize>(size));
                if (!stream.good())
                {
                    return Result<void>(ErrorCode::FileCorrupted, "AssetRegistryCache: failed to read string payload");
                }
            }

            return Result<void>();
        }
    }

    std::filesystem::path AssetRegistryCache::GetCacheFilePath(const std::filesystem::path& databaseFilePath)
    {
        if (databaseFilePath.empty())
        {
            return {};
        }

        return databaseFilePath.parent_path() / "AssetRegistry.bin";
    }

    Result<AssetRegistryCacheSnapshot> AssetRegistryCache::LoadFromFile(const std::filesystem::path& cacheFilePath,
                                                                        uint32_t expectedDatabaseJsonVersion,
                                                                        uint64_t expectedSourceSizeBytes,
                                                                        int64_t expectedSourceLastWriteTimeTicks)
    {
        if (cacheFilePath.empty())
        {
            return Result<AssetRegistryCacheSnapshot>(ErrorCode::InvalidArgument, "AssetRegistryCache: cache path is empty");
        }
        if (!std::filesystem::exists(cacheFilePath))
        {
            return Result<AssetRegistryCacheSnapshot>(ErrorCode::FileNotFound, "AssetRegistryCache: cache file not found");
        }

        std::ifstream stream(cacheFilePath, std::ios::in | std::ios::binary);
        if (!stream.is_open())
        {
            return Result<AssetRegistryCacheSnapshot>(ErrorCode::FileAccessDenied, "AssetRegistryCache: failed to open cache file");
        }

        char magic[4]{};
        stream.read(magic, static_cast<std::streamsize>(sizeof(magic)));
        if (!stream.good())
        {
            return Result<AssetRegistryCacheSnapshot>(ErrorCode::FileCorrupted, "AssetRegistryCache: failed to read cache header");
        }
        if (!std::equal(std::begin(magic), std::end(magic), kMagic.begin()))
        {
            return Result<AssetRegistryCacheSnapshot>(ErrorCode::FileCorrupted, "AssetRegistryCache: invalid cache magic");
        }

        uint32_t schemaVersion = 0;
        auto readSchema = ReadValue(stream, schemaVersion);
        if (readSchema.IsFailure())
        {
            return Result<AssetRegistryCacheSnapshot>(readSchema.GetError());
        }
        if (schemaVersion != kSchemaVersion)
        {
            return Result<AssetRegistryCacheSnapshot>(ErrorCode::ResourceVersionMismatch, "AssetRegistryCache: unsupported cache schema version");
        }

        AssetRegistryCacheSnapshot snapshot{};
        auto readDbVersion = ReadValue(stream, snapshot.DatabaseJsonVersion);
        if (readDbVersion.IsFailure())
        {
            return Result<AssetRegistryCacheSnapshot>(readDbVersion.GetError());
        }
        if (snapshot.DatabaseJsonVersion != expectedDatabaseJsonVersion)
        {
            return Result<AssetRegistryCacheSnapshot>(ErrorCode::ResourceVersionMismatch, "AssetRegistryCache: database version mismatch");
        }

        auto readSourceSize = ReadValue(stream, snapshot.SourceSizeBytes);
        if (readSourceSize.IsFailure())
        {
            return Result<AssetRegistryCacheSnapshot>(readSourceSize.GetError());
        }
        if (snapshot.SourceSizeBytes != expectedSourceSizeBytes)
        {
            return Result<AssetRegistryCacheSnapshot>(ErrorCode::ResourceVersionMismatch, "AssetRegistryCache: source size mismatch");
        }

        auto readSourceTime = ReadValue(stream, snapshot.SourceLastWriteTimeTicks);
        if (readSourceTime.IsFailure())
        {
            return Result<AssetRegistryCacheSnapshot>(readSourceTime.GetError());
        }
        if (snapshot.SourceLastWriteTimeTicks != expectedSourceLastWriteTimeTicks)
        {
            return Result<AssetRegistryCacheSnapshot>(ErrorCode::ResourceVersionMismatch, "AssetRegistryCache: source timestamp mismatch");
        }

        uint32_t entryCount = 0;
        auto readEntryCount = ReadValue(stream, entryCount);
        if (readEntryCount.IsFailure())
        {
            return Result<AssetRegistryCacheSnapshot>(readEntryCount.GetError());
        }

        snapshot.Entries.reserve(entryCount);
        for (uint32_t entryIndex = 0; entryIndex < entryCount; ++entryIndex)
        {
            AssetRegistryCacheEntry entry{};

            auto readGuid = ReadString(stream, entry.Guid);
            if (readGuid.IsFailure())
            {
                return Result<AssetRegistryCacheSnapshot>(readGuid.GetError());
            }
            auto readKey = ReadString(stream, entry.Key);
            if (readKey.IsFailure())
            {
                return Result<AssetRegistryCacheSnapshot>(readKey.GetError());
            }
            auto readResolvedPath = ReadString(stream, entry.ResolvedPath);
            if (readResolvedPath.IsFailure())
            {
                return Result<AssetRegistryCacheSnapshot>(readResolvedPath.GetError());
            }

            uint32_t rawType = 0;
            auto readType = ReadValue(stream, rawType);
            if (readType.IsFailure())
            {
                return Result<AssetRegistryCacheSnapshot>(readType.GetError());
            }
            entry.Type = static_cast<AssetType>(rawType);

            auto readSettings = ReadString(stream, entry.ImporterSettingsJson);
            if (readSettings.IsFailure())
            {
                return Result<AssetRegistryCacheSnapshot>(readSettings.GetError());
            }

            uint32_t dependencyCount = 0;
            auto readDependencyCount = ReadValue(stream, dependencyCount);
            if (readDependencyCount.IsFailure())
            {
                return Result<AssetRegistryCacheSnapshot>(readDependencyCount.GetError());
            }
            entry.Dependencies.reserve(dependencyCount);
            for (uint32_t dependencyIndex = 0; dependencyIndex < dependencyCount; ++dependencyIndex)
            {
                std::string dependency;
                auto readDependency = ReadString(stream, dependency);
                if (readDependency.IsFailure())
                {
                    return Result<AssetRegistryCacheSnapshot>(readDependency.GetError());
                }
                entry.Dependencies.push_back(std::move(dependency));
            }

            auto readSourceBytes = ReadValue(stream, entry.SourceSizeBytes);
            if (readSourceBytes.IsFailure())
            {
                return Result<AssetRegistryCacheSnapshot>(readSourceBytes.GetError());
            }
            auto readSourceTicks = ReadValue(stream, entry.SourceLastWriteTimeTicks);
            if (readSourceTicks.IsFailure())
            {
                return Result<AssetRegistryCacheSnapshot>(readSourceTicks.GetError());
            }
            auto readSettingsHash = ReadValue(stream, entry.ImporterSettingsHash64);
            if (readSettingsHash.IsFailure())
            {
                return Result<AssetRegistryCacheSnapshot>(readSettingsHash.GetError());
            }
            auto readImporterVersion = ReadValue(stream, entry.ImporterVersion);
            if (readImporterVersion.IsFailure())
            {
                return Result<AssetRegistryCacheSnapshot>(readImporterVersion.GetError());
            }
            auto readSourceKind = ReadValue(stream, entry.SourceKind);
            if (readSourceKind.IsFailure())
            {
                return Result<AssetRegistryCacheSnapshot>(readSourceKind.GetError());
            }

            snapshot.Entries.push_back(std::move(entry));
        }

        return snapshot;
    }

    Result<void> AssetRegistryCache::SaveToFile(const std::filesystem::path& cacheFilePath,
                                                const AssetRegistryCacheSnapshot& snapshot)
    {
        if (cacheFilePath.empty())
        {
            return Result<void>(ErrorCode::InvalidArgument, "AssetRegistryCache: cache path is empty");
        }

        try
        {
            if (cacheFilePath.has_parent_path())
            {
                std::filesystem::create_directories(cacheFilePath.parent_path());
            }

            const std::filesystem::path tempPath = cacheFilePath.string() + ".tmp";
            std::ofstream stream(tempPath, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!stream.is_open())
            {
                return Result<void>(ErrorCode::FileAccessDenied, "AssetRegistryCache: failed to open temp cache file for write");
            }

            stream.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
            if (!stream.good())
            {
                return Result<void>(ErrorCode::FileAccessDenied, "AssetRegistryCache: failed to write cache magic");
            }

            auto writeSchemaVersion = WriteValue(stream, kSchemaVersion);
            if (writeSchemaVersion.IsFailure())
            {
                return writeSchemaVersion;
            }
            auto writeDbVersion = WriteValue(stream, snapshot.DatabaseJsonVersion);
            if (writeDbVersion.IsFailure())
            {
                return writeDbVersion;
            }
            auto writeSourceSize = WriteValue(stream, snapshot.SourceSizeBytes);
            if (writeSourceSize.IsFailure())
            {
                return writeSourceSize;
            }
            auto writeSourceTime = WriteValue(stream, snapshot.SourceLastWriteTimeTicks);
            if (writeSourceTime.IsFailure())
            {
                return writeSourceTime;
            }

            if (snapshot.Entries.size() > std::numeric_limits<uint32_t>::max())
            {
                return Result<void>(ErrorCode::FileTooLarge, "AssetRegistryCache: too many entries to serialize");
            }
            const uint32_t entryCount = static_cast<uint32_t>(snapshot.Entries.size());
            auto writeEntryCount = WriteValue(stream, entryCount);
            if (writeEntryCount.IsFailure())
            {
                return writeEntryCount;
            }

            for (const auto& entry : snapshot.Entries)
            {
                auto writeGuid = WriteString(stream, entry.Guid);
                if (writeGuid.IsFailure())
                {
                    return writeGuid;
                }
                auto writeKey = WriteString(stream, entry.Key);
                if (writeKey.IsFailure())
                {
                    return writeKey;
                }
                auto writeResolvedPath = WriteString(stream, entry.ResolvedPath);
                if (writeResolvedPath.IsFailure())
                {
                    return writeResolvedPath;
                }

                const uint32_t rawType = static_cast<uint32_t>(entry.Type);
                auto writeType = WriteValue(stream, rawType);
                if (writeType.IsFailure())
                {
                    return writeType;
                }

                auto writeSettings = WriteString(stream, entry.ImporterSettingsJson);
                if (writeSettings.IsFailure())
                {
                    return writeSettings;
                }

                if (entry.Dependencies.size() > std::numeric_limits<uint32_t>::max())
                {
                    return Result<void>(ErrorCode::FileTooLarge, "AssetRegistryCache: too many dependencies to serialize");
                }
                const uint32_t dependencyCount = static_cast<uint32_t>(entry.Dependencies.size());
                auto writeDependencyCount = WriteValue(stream, dependencyCount);
                if (writeDependencyCount.IsFailure())
                {
                    return writeDependencyCount;
                }
                for (const auto& dependency : entry.Dependencies)
                {
                    auto writeDependency = WriteString(stream, dependency);
                    if (writeDependency.IsFailure())
                    {
                        return writeDependency;
                    }
                }

                auto writeSourceBytes = WriteValue(stream, entry.SourceSizeBytes);
                if (writeSourceBytes.IsFailure())
                {
                    return writeSourceBytes;
                }
                auto writeSourceTicks = WriteValue(stream, entry.SourceLastWriteTimeTicks);
                if (writeSourceTicks.IsFailure())
                {
                    return writeSourceTicks;
                }
                auto writeSettingsHash = WriteValue(stream, entry.ImporterSettingsHash64);
                if (writeSettingsHash.IsFailure())
                {
                    return writeSettingsHash;
                }
                auto writeImporterVersion = WriteValue(stream, entry.ImporterVersion);
                if (writeImporterVersion.IsFailure())
                {
                    return writeImporterVersion;
                }
                auto writeSourceKind = WriteValue(stream, entry.SourceKind);
                if (writeSourceKind.IsFailure())
                {
                    return writeSourceKind;
                }
            }

            stream.flush();
            if (!stream.good())
            {
                return Result<void>(ErrorCode::FileAccessDenied, "AssetRegistryCache: failed to flush temp cache file");
            }
            stream.close();

            std::error_code errorCode;
            std::filesystem::rename(tempPath, cacheFilePath, errorCode);
            if (errorCode)
            {
                errorCode.clear();
                std::filesystem::remove(cacheFilePath, errorCode);
                errorCode.clear();
                std::filesystem::rename(tempPath, cacheFilePath, errorCode);
                if (errorCode)
                {
                    return Result<void>(ErrorCode::FileAccessDenied, "AssetRegistryCache: failed to replace cache file: " + errorCode.message());
                }
            }
        }
        catch (const std::exception& exception)
        {
            return Result<void>(ErrorCode::FileAccessDenied, std::string("AssetRegistryCache: save failed: ") + exception.what());
        }

        return Result<void>();
    }
}
