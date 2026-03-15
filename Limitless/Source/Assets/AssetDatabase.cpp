#include "Assets/AssetDatabase.h"

#include "Assets/AssetBundle.h"
#include "Assets/AssetImporterVersion.h"
#include "Assets/AssetPaths.h"
#include "Assets/AssetRegistryCache.h"
#include "Assets/AssetUtils.h"
#include "Project/ProjectManager.h"

#include "Core/Debug/Log.h"
#include "Core/Hash/XxHash64.h"

#include <algorithm>
#include <fstream>

namespace Limitless::Assets
{
    namespace
    {
        constexpr uint32_t kAssetDatabaseJsonVersion = 2;
    }

    static uint64_t ComputeImporterSettingsHash64(const nlohmann::json& importerSettings)
    {
        // Stable across restarts; used only for incremental tooling decisions.
        const std::string settingsText = importerSettings.dump();
        ::Limitless::Hash::XxHash64::State hasher(0);
        hasher.Update(settingsText.data(), settingsText.size());
        return hasher.Digest();
    }

    static int64_t GetLastWriteTimeTicksOrZero(const std::filesystem::path& path)
    {
        std::error_code ec;
        const auto t = std::filesystem::last_write_time(path, ec);
        if (ec)
        {
            return 0;
        }

        // file_time_type::duration::rep is implementation-defined; we store the raw tick count and only
        // compare values from the same platform/toolchain family (incremental reimport correctness is best-effort).
        return static_cast<int64_t>(t.time_since_epoch().count());
    }

    static uint64_t GetFileSizeOrZero(const std::filesystem::path& path)
    {
        std::error_code ec;
        const uint64_t size = std::filesystem::file_size(path, ec);
        return ec ? 0ull : size;
    }

    static AssetRegistryCacheSnapshot BuildRegistryCacheSnapshot(
        const std::unordered_map<std::string, AssetDatabase::Record>& records,
        uint64_t sourceSizeBytes,
        int64_t sourceLastWriteTimeTicks)
    {
        AssetRegistryCacheSnapshot snapshot{};
        snapshot.DatabaseJsonVersion = kAssetDatabaseJsonVersion;
        snapshot.SourceSizeBytes = sourceSizeBytes;
        snapshot.SourceLastWriteTimeTicks = sourceLastWriteTimeTicks;
        snapshot.Entries.reserve(records.size());

        std::vector<const AssetDatabase::Record*> sortedRecords;
        sortedRecords.reserve(records.size());
        for (const auto& [guid, record] : records)
        {
            (void)guid;
            sortedRecords.push_back(&record);
        }

        std::sort(sortedRecords.begin(), sortedRecords.end(), [](const AssetDatabase::Record* lhs, const AssetDatabase::Record* rhs) {
            if (lhs->Key != rhs->Key)
                return lhs->Key < rhs->Key;
            return lhs->Guid < rhs->Guid;
        });

        for (const AssetDatabase::Record* record : sortedRecords)
        {
            AssetRegistryCacheEntry entry{};
            entry.Guid = record->Guid;
            entry.Key = record->Key;
            entry.ResolvedPath = record->ResolvedPath;
            entry.Type = record->Type;
            entry.ImporterSettingsJson = record->ImporterSettings.dump();
            entry.Dependencies = record->Dependencies;
            entry.SourceSizeBytes = record->SourceSizeBytes;
            entry.SourceLastWriteTimeTicks = record->SourceLastWriteTimeTicks;
            entry.ImporterSettingsHash64 = record->ImporterSettingsHash64;
            entry.ImporterVersion = record->ImporterVersion;
            entry.SourceKind = static_cast<uint8_t>(record->SourceKind);
            snapshot.Entries.push_back(std::move(entry));
        }

        return snapshot;
    }

    static bool IsPathUnderRoot(const std::filesystem::path& candidatePath,
                                const std::filesystem::path& rootPath)
    {
        std::error_code ec;
        const std::filesystem::path candidate = std::filesystem::weakly_canonical(candidatePath, ec);
        if (ec)
        {
            return false;
        }

        ec.clear();
        const std::filesystem::path root = std::filesystem::weakly_canonical(rootPath, ec);
        if (ec)
        {
            return false;
        }

        ec.clear();
        const std::filesystem::path rel = std::filesystem::relative(candidate, root, ec);
        if (ec)
        {
            return false;
        }

        if (rel.empty())
        {
            return true;
        }

        const std::string relText = rel.generic_string();
        return !(relText == ".." || relText.rfind("../", 0) == 0);
    }

    AssetDatabase& AssetDatabase::GetInstance()
    {
        static AssetDatabase s_Instance;
        return s_Instance;
    }

    void AssetDatabase::EnsureLoaded()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_Loaded)
        {
            return;
        }

        // Bundle-only runtime:
        // The persistent database is a tooling/import-time concept. In shipping/bundle scenarios the
        // project root (and thus Build/AssetDatabase.json) may not exist, and we should not warn.
        // Dependencies are expected to be satisfied via the bundle manifest.
        const auto& bundle = AssetBundle::GetInstance();
        if (bundle.IsEnabled() && bundle.IsLoaded())
        {
            m_Loaded = true;
            return;
        }

        const auto loadResult = LoadFromDiskLocked();
        if (loadResult.IsFailure())
        {
            // Non-fatal: missing database is expected on first run.
            LT_CORE_WARN("AssetDatabase: starting with empty database (load failed): {}",
                         loadResult.GetError().GetErrorMessage());
        }

        m_Loaded = true;
    }

    void AssetDatabase::Reset()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_ByGuid.clear();
        m_GuidByKey.clear();
        m_DependentsByGuid.clear();
        m_Loaded = false;
        ++m_Revision;
    }

    Result<AssetDatabase::Record> AssetDatabase::FindByGuid(const std::string& guid)
    {
        EnsureLoaded();
        std::lock_guard<std::mutex> lock(m_Mutex);

        const auto it = m_ByGuid.find(guid);
        if (it == m_ByGuid.end())
        {
            return Result<Record>(ErrorCode::ResourceNotFound, "AssetDatabase: GUID not found");
        }
        return it->second;
    }

    Result<AssetDatabase::Record> AssetDatabase::FindByKey(const std::string& key)
    {
        EnsureLoaded();
        std::lock_guard<std::mutex> lock(m_Mutex);

        const auto it = m_GuidByKey.find(key);
        if (it == m_GuidByKey.end())
        {
            return Result<Record>(ErrorCode::ResourceNotFound, "AssetDatabase: key not found");
        }

        const auto git = m_ByGuid.find(it->second);
        if (git == m_ByGuid.end())
        {
            return Result<Record>(ErrorCode::ResourceCorrupted, "AssetDatabase: key->guid mapping is stale");
        }

        return git->second;
    }

    Result<AssetDatabase::Record> AssetDatabase::ImportOrUpdate(const std::string& key,
                                                                AssetType type,
                                                                const nlohmann::json& importerSettings,
                                                                uint32_t importerVersion)
    {
        if (key.empty())
        {
            return Result<Record>(ErrorCode::InvalidArgument, "AssetDatabase::ImportOrUpdate: key is empty");
        }

        EnsureLoaded();

        // Resolve key and ensure GUID.
        const auto resolvedPathResult = ResolveAssetKeyToPath(key);
        if (resolvedPathResult.IsFailure())
        {
            return Result<Record>(resolvedPathResult.GetError());
        }
        const std::filesystem::path resolvedPath = resolvedPathResult.GetValue();

        if (!std::filesystem::exists(resolvedPath))
        {
            return Result<Record>(ErrorCode::FileNotFound, "Asset file not found: " + resolvedPath.string());
        }

        // Database records are project-local; do not allow keys resolved outside
        // the active project's Assets/ tree.
        std::filesystem::path projectRootForValidation;
        const auto& projectManager = Project::ProjectManager::GetInstance();
        if (projectManager.HasOpenProject())
        {
            projectRootForValidation = projectManager.GetProjectRoot();
        }
        else
        {
            const auto rootResult = FindProjectRootFromWorkingDirectory();
            if (rootResult.IsSuccess())
                projectRootForValidation = rootResult.GetValue();
        }

        if (!projectRootForValidation.empty())
        {
            const std::filesystem::path projectAssetsRoot = projectRootForValidation / "Assets";
            if (!IsPathUnderRoot(resolvedPath, projectAssetsRoot))
            {
                return Result<Record>(
                    ErrorCode::InvalidState,
                    "AssetDatabase::ImportOrUpdate: resolved path is outside active project Assets/: " + resolvedPath.string());
            }
        }

        std::error_code sizeEc;
        const uint64_t sizeBytes = std::filesystem::file_size(resolvedPath, sizeEc);
        const int64_t lastWriteTicks = GetLastWriteTimeTicksOrZero(resolvedPath);

        const auto guidResult = LoadOrCreateGuid(resolvedPath.string(), {{"key", key}, {"type", ToString(type)}});
        if (guidResult.IsFailure())
        {
            return Result<Record>(guidResult.GetError());
        }

        const uint32_t resolvedImporterVersion =
            importerVersion != 0u ? importerVersion : GetCurrentAssetImporterVersion(type);

        Record record;
        record.Guid = guidResult.GetValue();
        record.Key = key;
        record.ResolvedPath = resolvedPath.string();
        record.Type = type;
        record.ImporterSettings = importerSettings;
        record.SourceSizeBytes = sizeEc ? 0ull : sizeBytes;
        record.SourceLastWriteTimeTicks = lastWriteTicks;
        record.ImporterSettingsHash64 = ComputeImporterSettingsHash64(record.ImporterSettings);
        record.ImporterVersion = resolvedImporterVersion != 0u ? resolvedImporterVersion : 1u;

        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            bool rebuildDependentsIndex = false;

            // If the key previously mapped to a different GUID, the `.meta` GUID changed on disk.
            // This can happen when a user intentionally regenerates a GUID (breaking references).
            // We treat this as a record identity change:
            // - The new GUID becomes canonical for the key
            // - The old record is removed from the database
            //
            // NOTE: We intentionally do NOT rewrite other assets' references; those should now be "missing"
            // and are surfaced via diagnostics.
            if (auto keyIt = m_GuidByKey.find(record.Key); keyIt != m_GuidByKey.end() && keyIt->second != record.Guid)
            {
                const std::string oldGuid = keyIt->second;
                if (auto oldIt = m_ByGuid.find(oldGuid); oldIt != m_ByGuid.end())
                {
                    // Preserve per-key metadata where safe.
                    if (record.Dependencies.empty())
                        record.Dependencies = oldIt->second.Dependencies;

                    // Preserve importer settings if caller passed empty object.
                    if (record.ImporterSettings.is_object() && record.ImporterSettings.empty())
                    {
                        record.ImporterSettings = oldIt->second.ImporterSettings;
                        record.ImporterSettingsHash64 = oldIt->second.ImporterSettingsHash64;
                    }

                    m_ByGuid.erase(oldIt);
                    rebuildDependentsIndex = true;
                }
            }

            // Preserve existing dependencies if present.
            if (auto it = m_ByGuid.find(record.Guid); it != m_ByGuid.end())
            {
                record.Dependencies = it->second.Dependencies;
                // Preserve existing import fingerprint if we could not compute it this pass.
                if (record.SourceSizeBytes == 0) record.SourceSizeBytes = it->second.SourceSizeBytes;
                if (record.SourceLastWriteTimeTicks == 0) record.SourceLastWriteTimeTicks = it->second.SourceLastWriteTimeTicks;
                if (record.ImporterSettingsHash64 == 0) record.ImporterSettingsHash64 = it->second.ImporterSettingsHash64;
                if (record.ImporterVersion == 0) record.ImporterVersion = it->second.ImporterVersion;

                // Preserve importer settings unless explicitly overridden.
                // This is critical because many tooling paths (bundle discovery, watchers, etc.)
                // may call ImportOrUpdate with an empty object just to ensure a GUID exists.
                if (record.ImporterSettings.is_object() && record.ImporterSettings.empty())
                {
                    record.ImporterSettings = it->second.ImporterSettings;
                    record.ImporterSettingsHash64 = it->second.ImporterSettingsHash64;
                }
            }

            // Keep GUID->key mapping canonical:
            // if this GUID was previously mapped from older keys (for example after file rename),
            // remove those stale key aliases so FindByKey(oldKey) no longer resolves.
            for (auto keyIt = m_GuidByKey.begin(); keyIt != m_GuidByKey.end();)
            {
                if (keyIt->second == record.Guid && keyIt->first != record.Key)
                    keyIt = m_GuidByKey.erase(keyIt);
                else
                    ++keyIt;
            }

            m_ByGuid[record.Guid] = record;
            m_GuidByKey[record.Key] = record.Guid;
            ++m_Revision;

            if (rebuildDependentsIndex)
            {
                RebuildDependentsIndexLocked();
            }

            const auto saveResult = SaveToDiskLocked();
            if (saveResult.IsFailure())
            {
                LT_CORE_WARN("AssetDatabase: failed to save database: {}", saveResult.GetError().GetErrorMessage());
            }
        }

        return record;
    }

    Result<void> AssetDatabase::SetDependencies(const std::string& guid, const std::vector<std::string>& dependencies)
    {
        if (guid.empty())
        {
            return Result<void>(ErrorCode::InvalidArgument, "AssetDatabase::SetDependencies: guid is empty");
        }

        EnsureLoaded();

        std::filesystem::path resolvedPath;
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            const auto it = m_ByGuid.find(guid);
            if (it == m_ByGuid.end())
            {
                return Result<void>(ErrorCode::ResourceNotFound, "AssetDatabase::SetDependencies: guid not found");
            }

            // Update reverse dependency index:
            // Remove old edges (dep -> guid) and add new edges.
            const auto oldDeps = it->second.Dependencies;
            for (const auto& dep : oldDeps)
            {
                auto dit = m_DependentsByGuid.find(dep);
                if (dit != m_DependentsByGuid.end())
                {
                    auto& vec = dit->second;
                    vec.erase(std::remove(vec.begin(), vec.end(), guid), vec.end());
                }
            }

            it->second.Dependencies = dependencies;
            resolvedPath = it->second.ResolvedPath;

            for (const auto& dep : dependencies)
            {
                if (dep.empty())
                {
                    continue;
                }
                auto& vec = m_DependentsByGuid[dep];
                if (std::find(vec.begin(), vec.end(), guid) == vec.end())
                {
                    vec.push_back(guid);
                }
            }

            const auto saveResult = SaveToDiskLocked();
            if (saveResult.IsFailure())
            {
                LT_CORE_WARN("AssetDatabase: failed to save database: {}", saveResult.GetError().GetErrorMessage());
            }
            ++m_Revision;
        }

        // Generated assets have no .meta file; deps are stored in the database only.
        if (resolvedPath.empty())
        {
            return Result<void>();
        }

        // Write deps into .meta next to the real asset.
        return WriteDependencies(resolvedPath.string(), dependencies);
    }

    std::vector<AssetDatabase::Record> AssetDatabase::GetDependentsOf(const std::string& guid)
    {
        EnsureLoaded();
        std::lock_guard<std::mutex> lock(m_Mutex);

        std::vector<Record> out;
        if (guid.empty())
        {
            return out;
        }

        const auto it = m_DependentsByGuid.find(guid);
        if (it == m_DependentsByGuid.end())
        {
            return out;
        }

        out.reserve(it->second.size());
        for (const auto& dependentGuid : it->second)
        {
            const auto recIt = m_ByGuid.find(dependentGuid);
            if (recIt != m_ByGuid.end())
            {
                out.push_back(recIt->second);
            }
        }

        return out;
    }

    std::vector<AssetDatabase::Record> AssetDatabase::GetAllRecords()
    {
        EnsureLoaded();
        std::lock_guard<std::mutex> lock(m_Mutex);

        std::vector<Record> out;
        out.reserve(m_ByGuid.size());
        for (const auto& [id, record] : m_ByGuid)
        {
            out.push_back(record);
        }
        return out;
    }

    std::vector<AssetDatabase::Record> AssetDatabase::CommitRecordBatch(std::vector<Record>& records)
    {
        if (records.empty())
            return {};

        EnsureLoaded();

        std::vector<Record> committed;
        committed.reserve(records.size());

        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            bool rebuildDependentsIndex = false;

            for (auto& record : records)
            {
                if (record.Guid.empty() || record.Key.empty())
                    continue;

                // Compute settings hash if not provided.
                if (record.ImporterSettingsHash64 == 0 && !record.ImporterSettings.empty())
                    record.ImporterSettingsHash64 = ComputeImporterSettingsHash64(record.ImporterSettings);

                if (record.ImporterVersion == 0)
                {
                    const uint32_t resolvedImporterVersion = GetCurrentAssetImporterVersion(record.Type);
                    record.ImporterVersion = resolvedImporterVersion != 0u ? resolvedImporterVersion : 1u;
                }

                // Handle key->GUID change (same logic as ImportOrUpdate).
                if (auto keyIt = m_GuidByKey.find(record.Key); keyIt != m_GuidByKey.end() && keyIt->second != record.Guid)
                {
                    const std::string oldGuid = keyIt->second;
                    if (auto oldIt = m_ByGuid.find(oldGuid); oldIt != m_ByGuid.end())
                    {
                        if (record.Dependencies.empty())
                            record.Dependencies = oldIt->second.Dependencies;
                        if (record.ImporterSettings.is_object() && record.ImporterSettings.empty())
                        {
                            record.ImporterSettings = oldIt->second.ImporterSettings;
                            record.ImporterSettingsHash64 = oldIt->second.ImporterSettingsHash64;
                        }
                        m_ByGuid.erase(oldIt);
                        rebuildDependentsIndex = true;
                    }
                }

                // Preserve existing data from current record (same logic as ImportOrUpdate).
                if (auto it = m_ByGuid.find(record.Guid); it != m_ByGuid.end())
                {
                    record.Dependencies = it->second.Dependencies;
                    if (record.SourceSizeBytes == 0) record.SourceSizeBytes = it->second.SourceSizeBytes;
                    if (record.SourceLastWriteTimeTicks == 0) record.SourceLastWriteTimeTicks = it->second.SourceLastWriteTimeTicks;
                    if (record.ImporterSettingsHash64 == 0) record.ImporterSettingsHash64 = it->second.ImporterSettingsHash64;
                    if (record.ImporterVersion == 0) record.ImporterVersion = it->second.ImporterVersion;
                    if (record.ImporterSettings.is_object() && record.ImporterSettings.empty())
                    {
                        record.ImporterSettings = it->second.ImporterSettings;
                        record.ImporterSettingsHash64 = it->second.ImporterSettingsHash64;
                    }
                }

                // Clean stale key aliases (same logic as ImportOrUpdate).
                for (auto keyIt = m_GuidByKey.begin(); keyIt != m_GuidByKey.end();)
                {
                    if (keyIt->second == record.Guid && keyIt->first != record.Key)
                        keyIt = m_GuidByKey.erase(keyIt);
                    else
                        ++keyIt;
                }

                m_ByGuid[record.Guid] = record;
                m_GuidByKey[record.Key] = record.Guid;
                committed.push_back(record);
            }

            if (rebuildDependentsIndex)
                RebuildDependentsIndexLocked();

            ++m_Revision;

            const auto saveResult = SaveToDiskLocked();
            if (saveResult.IsFailure())
            {
                LT_CORE_WARN("AssetDatabase: failed to save database after batch commit: {}", saveResult.GetError().GetErrorMessage());
            }
        }

        return committed;
    }

    Result<AssetDatabase::Record> AssetDatabase::RegisterGeneratedAsset(const std::string& guid,
                                                                        const std::string& virtualKey,
                                                                        AssetType type,
                                                                        const nlohmann::json& importerSettings,
                                                                        uint32_t importerVersion)
    {
        if (guid.empty())
        {
            return Result<Record>(ErrorCode::InvalidArgument, "AssetDatabase::RegisterGeneratedAsset: guid is empty");
        }
        if (virtualKey.empty())
        {
            return Result<Record>(ErrorCode::InvalidArgument, "AssetDatabase::RegisterGeneratedAsset: virtualKey is empty");
        }

        EnsureLoaded();

        const uint32_t resolvedImporterVersion =
            importerVersion != 0u ? importerVersion : GetCurrentAssetImporterVersion(type);

        Record record;
        record.Guid = guid;
        record.Key = virtualKey;
        record.ResolvedPath = {};  // no file on disk
        record.Type = type;
        record.SourceKind = AssetSourceKind::Generated;
        record.ImporterSettings = importerSettings;
        record.ImporterSettingsHash64 = ComputeImporterSettingsHash64(importerSettings);
        record.ImporterVersion = resolvedImporterVersion != 0u ? resolvedImporterVersion : 1u;

        {
            std::lock_guard<std::mutex> lock(m_Mutex);

            // If the key previously mapped to a different GUID, clean up the old record.
            if (auto keyIt = m_GuidByKey.find(record.Key); keyIt != m_GuidByKey.end() && keyIt->second != record.Guid)
            {
                const std::string oldGuid = keyIt->second;
                m_ByGuid.erase(oldGuid);
            }

            // Preserve existing dependencies if this GUID already exists.
            if (auto it = m_ByGuid.find(record.Guid); it != m_ByGuid.end())
            {
                if (record.Dependencies.empty())
                    record.Dependencies = it->second.Dependencies;
                if (record.ImporterSettings.is_object() && record.ImporterSettings.empty())
                {
                    record.ImporterSettings = it->second.ImporterSettings;
                    record.ImporterSettingsHash64 = it->second.ImporterSettingsHash64;
                }
            }

            // Clean stale key aliases.
            for (auto keyIt = m_GuidByKey.begin(); keyIt != m_GuidByKey.end();)
            {
                if (keyIt->second == record.Guid && keyIt->first != record.Key)
                    keyIt = m_GuidByKey.erase(keyIt);
                else
                    ++keyIt;
            }

            m_ByGuid[record.Guid] = record;
            m_GuidByKey[record.Key] = record.Guid;
            ++m_Revision;

            const auto saveResult = SaveToDiskLocked();
            if (saveResult.IsFailure())
            {
                LT_CORE_WARN("AssetDatabase: failed to save database: {}", saveResult.GetError().GetErrorMessage());
            }
        }

        return record;
    }

    Result<void> AssetDatabase::RemoveByGuid(const std::string& guid)
    {
        if (guid.empty())
        {
            return Result<void>(ErrorCode::InvalidArgument, "AssetDatabase::RemoveByGuid: guid is empty");
        }

        EnsureLoaded();
        std::lock_guard<std::mutex> lock(m_Mutex);

        const auto it = m_ByGuid.find(guid);
        if (it == m_ByGuid.end())
        {
            return Result<void>(ErrorCode::ResourceNotFound, "AssetDatabase::RemoveByGuid: guid not found");
        }

        const std::string key = it->second.Key;
        m_ByGuid.erase(it);
        if (!key.empty())
        {
            m_GuidByKey.erase(key);
        }

        RebuildDependentsIndexLocked();
        ++m_Revision;

        const auto saveResult = SaveToDiskLocked();
        if (saveResult.IsFailure())
        {
            LT_CORE_WARN("AssetDatabase: failed to save database: {}", saveResult.GetError().GetErrorMessage());
        }

        return Result<void>();
    }

    Result<void> AssetDatabase::RemoveByKey(const std::string& key)
    {
        if (key.empty())
        {
            return Result<void>(ErrorCode::InvalidArgument, "AssetDatabase::RemoveByKey: key is empty");
        }

        EnsureLoaded();
        std::lock_guard<std::mutex> lock(m_Mutex);

        const auto it = m_GuidByKey.find(key);
        if (it == m_GuidByKey.end())
        {
            return Result<void>(ErrorCode::ResourceNotFound, "AssetDatabase::RemoveByKey: key not found");
        }

        const std::string guid = it->second;
        m_GuidByKey.erase(it);
        if (!guid.empty())
        {
            m_ByGuid.erase(guid);
        }

        RebuildDependentsIndexLocked();
        ++m_Revision;

        const auto saveResult = SaveToDiskLocked();
        if (saveResult.IsFailure())
        {
            LT_CORE_WARN("AssetDatabase: failed to save database: {}", saveResult.GetError().GetErrorMessage());
        }

        return Result<void>();
    }

    size_t AssetDatabase::GetRecordCount() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_ByGuid.size();
    }

    uint64_t AssetDatabase::GetRevision() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Revision;
    }

    AssetDatabase::CacheTelemetry AssetDatabase::GetCacheTelemetry() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return { m_CacheHits, m_CacheMisses };
    }

    Result<std::filesystem::path> AssetDatabase::GetDatabaseFilePathLocked()
    {
        // Prefer the explicitly opened project root when available; this avoids
        // any ambiguity from working-directory heuristics during project switches.
        const auto& projectManager = Project::ProjectManager::GetInstance();
        if (projectManager.HasOpenProject())
        {
            const std::filesystem::path root = projectManager.GetProjectRoot();
            return root / "Build" / "AssetDatabase.json";
        }

        const auto rootResult = FindProjectRootFromWorkingDirectory();
        if (rootResult.IsFailure())
        {
            return Result<std::filesystem::path>(rootResult.GetError());
        }

        // Keep it out of Assets/ (Unity stores this in Library/). We use Build/ for now.
        const std::filesystem::path root = rootResult.GetValue();
        return root / "Build" / "AssetDatabase.json";
    }

    Result<void> AssetDatabase::LoadFromDiskLocked()
    {
        const auto pathResult = GetDatabaseFilePathLocked();
        if (pathResult.IsFailure())
        {
            return Result<void>(pathResult.GetError());
        }
        const std::filesystem::path dbPath = pathResult.GetValue();
        const std::filesystem::path projectRoot = dbPath.parent_path().parent_path();
        const std::filesystem::path projectAssetsRoot = projectRoot / "Assets";

        if (!std::filesystem::exists(dbPath))
        {
            return Result<void>(ErrorCode::FileNotFound, "AssetDatabase file not found");
        }

        try
        {
            const uint64_t sourceSizeBytes = GetFileSizeOrZero(dbPath);
            const int64_t sourceLastWriteTimeTicks = GetLastWriteTimeTicksOrZero(dbPath);
            const std::filesystem::path cachePath = AssetRegistryCache::GetCacheFilePath(dbPath);

            auto loadFromCache = [&]() -> Result<void>
            {
                const auto cacheLoadResult = AssetRegistryCache::LoadFromFile(
                    cachePath,
                    kAssetDatabaseJsonVersion,
                    sourceSizeBytes,
                    sourceLastWriteTimeTicks);
                if (cacheLoadResult.IsFailure())
                {
                    return Result<void>(cacheLoadResult.GetError());
                }

                m_ByGuid.clear();
                m_GuidByKey.clear();
                m_DependentsByGuid.clear();

                for (const auto& entry : cacheLoadResult.GetValue().Entries)
                {
                    Record record{};
                    record.Guid = entry.Guid;
                    record.Key = entry.Key;
                    record.ResolvedPath = entry.ResolvedPath;
                    record.Type = entry.Type;
                    if (!entry.ImporterSettingsJson.empty())
                    {
                        try
                        {
                            record.ImporterSettings = nlohmann::json::parse(entry.ImporterSettingsJson);
                        }
                        catch (const std::exception& exception)
                        {
                            return Result<void>(ErrorCode::FileCorrupted,
                                                std::string("AssetRegistryCache: failed to parse importer settings JSON: ") + exception.what());
                        }
                        if (!record.ImporterSettings.is_object())
                        {
                            return Result<void>(ErrorCode::FileCorrupted, "AssetRegistryCache: importer settings payload is not a JSON object");
                        }
                    }
                    else
                    {
                        record.ImporterSettings = nlohmann::json::object();
                    }
                    record.Dependencies = entry.Dependencies;
                    record.SourceSizeBytes = entry.SourceSizeBytes;
                    record.SourceLastWriteTimeTicks = entry.SourceLastWriteTimeTicks;
                    record.ImporterSettingsHash64 = entry.ImporterSettingsHash64;
                    record.ImporterVersion = entry.ImporterVersion;
                    record.SourceKind = static_cast<AssetSourceKind>(entry.SourceKind);

                    if (record.Guid.empty() || record.Key.empty())
                    {
                        continue;
                    }

                    // Generated assets have no ResolvedPath and are not scoped to Assets/.
                    if (record.SourceKind != AssetSourceKind::Generated)
                    {
                        if (record.ResolvedPath.empty() || !IsPathUnderRoot(std::filesystem::path(record.ResolvedPath), projectAssetsRoot))
                        {
                            continue;
                        }
                    }

                    m_GuidByKey[record.Key] = record.Guid;
                    m_ByGuid[record.Guid] = std::move(record);
                }

                RebuildDependentsIndexLocked();
                return Result<void>();
            };

            const auto cacheResult = loadFromCache();
            if (cacheResult.IsSuccess())
            {
                ++m_CacheHits;
                ++m_Revision;
                return Result<void>();
            }

            ++m_CacheMisses;

            const ErrorCode cacheErrorCode = cacheResult.GetError().GetCode();
            if (cacheErrorCode != ErrorCode::FileNotFound &&
                cacheErrorCode != ErrorCode::ResourceVersionMismatch)
            {
                LT_CORE_WARN("AssetDatabase: binary cache load failed, falling back to JSON: {}",
                             cacheResult.GetError().GetErrorMessage());
            }

            std::ifstream in(dbPath, std::ios::in | std::ios::binary);
            if (!in.is_open())
            {
                return Result<void>(ErrorCode::FileAccessDenied, "Failed to open AssetDatabase: " + dbPath.string());
            }

            nlohmann::json root;
            in >> root;

            if (!root.contains("records") || !root["records"].is_array())
            {
                return Result<void>(ErrorCode::FileCorrupted, "AssetDatabase: missing 'records' array");
            }

            m_ByGuid.clear();
            m_GuidByKey.clear();
            m_DependentsByGuid.clear();

            for (const auto& recJson : root["records"])
            {
                const auto recResult = RecordFromJson(recJson);
                if (recResult.IsFailure())
                {
                    LT_CORE_WARN("AssetDatabase: skipping invalid record: {}", recResult.GetError().GetErrorMessage());
                    continue;
                }

                Record r = recResult.GetValue();
                if (r.Guid.empty() || r.Key.empty())
                {
                    continue;
                }

                // Keep database scoped to the currently opened project even if
                // stale/mixed records are present in the loaded file.
                if (r.SourceKind != AssetSourceKind::Generated)
                {
                    if (r.ResolvedPath.empty() || !IsPathUnderRoot(std::filesystem::path(r.ResolvedPath), projectAssetsRoot))
                    {
                        continue;
                    }
                }

                m_GuidByKey[r.Key] = r.Guid;
                m_ByGuid[r.Guid] = std::move(r);
            }
            RebuildDependentsIndexLocked();
            ++m_Revision;

            const auto cacheSaveResult = AssetRegistryCache::SaveToFile(
                cachePath,
                BuildRegistryCacheSnapshot(m_ByGuid, sourceSizeBytes, sourceLastWriteTimeTicks));
            if (cacheSaveResult.IsFailure())
            {
                LT_CORE_WARN("AssetDatabase: failed to save binary cache: {}",
                             cacheSaveResult.GetError().GetErrorMessage());
            }
        }
        catch (const std::exception& e)
        {
            return Result<void>(ErrorCode::FileCorrupted, std::string("AssetDatabase parse error: ") + e.what());
        }

        return Result<void>();
    }

    Result<void> AssetDatabase::SaveToDiskLocked()
    {
        const auto pathResult = GetDatabaseFilePathLocked();
        if (pathResult.IsFailure())
        {
            return Result<void>(pathResult.GetError());
        }
        const std::filesystem::path dbPath = pathResult.GetValue();

        try
        {
            if (dbPath.has_parent_path())
            {
                std::filesystem::create_directories(dbPath.parent_path());
            }

            nlohmann::json root;
            root["version"] = kAssetDatabaseJsonVersion;
            root["records"] = nlohmann::json::array();

            for (const auto& [guid, record] : m_ByGuid)
            {
                root["records"].push_back(RecordToJson(record));
            }

            // Atomic save (best-effort):
            // - Write to temp file in the same directory
            // - Replace the destination with a rename
            //
            // Notes:
            // - On POSIX, rename() is atomic and replaces.
            // - On Windows, std::filesystem::rename fails if destination exists, so we do a remove+rename.
            //   This is not perfectly atomic on Windows, but it prevents partial writes and is much more robust
            //   than writing directly to the destination.
            const std::filesystem::path tmpPath = dbPath.string() + ".tmp";
            {
                std::ofstream out(tmpPath, std::ios::out | std::ios::binary | std::ios::trunc);
                if (!out.is_open())
                {
                    return Result<void>(ErrorCode::FileAccessDenied, "Failed to write AssetDatabase temp: " + tmpPath.string());
                }
                out << root.dump(4);
                out.flush();
            }

            std::error_code ec;
            std::filesystem::rename(tmpPath, dbPath, ec);
            if (ec)
            {
                // Windows replacement path.
                ec.clear();
                std::filesystem::remove(dbPath, ec);
                ec.clear();
                std::filesystem::rename(tmpPath, dbPath, ec);
                if (ec)
                {
                    // Leave temp for debugging if rename fails.
                    return Result<void>(ErrorCode::FileAccessDenied, "Failed to replace AssetDatabase: " + ec.message());
                }
            }

            const auto cacheSaveResult = AssetRegistryCache::SaveToFile(
                AssetRegistryCache::GetCacheFilePath(dbPath),
                BuildRegistryCacheSnapshot(
                    m_ByGuid,
                    GetFileSizeOrZero(dbPath),
                    GetLastWriteTimeTicksOrZero(dbPath)));
            if (cacheSaveResult.IsFailure())
            {
                LT_CORE_WARN("AssetDatabase: failed to save binary cache: {}",
                             cacheSaveResult.GetError().GetErrorMessage());
            }
        }
        catch (const std::exception& e)
        {
            return Result<void>(ErrorCode::FileAccessDenied, std::string("AssetDatabase save error: ") + e.what());
        }

        return Result<void>();
    }

    nlohmann::json AssetDatabase::RecordToJson(const Record& r)
    {
        nlohmann::json j;
        j["guid"] = r.Guid;
        j["key"] = r.Key;
        j["resolvedPath"] = r.ResolvedPath;
        j["type"] = ToString(r.Type);
        j["importerSettings"] = r.ImporterSettings;
        j["deps"] = r.Dependencies;
        j["sourceSizeBytes"] = r.SourceSizeBytes;
        j["sourceLastWriteTimeTicks"] = r.SourceLastWriteTimeTicks;
        j["importerSettingsHash64"] = r.ImporterSettingsHash64;
        j["importerVersion"] = r.ImporterVersion;
        j["sourceKind"] = ToString(r.SourceKind);
        return j;
    }

    Result<AssetDatabase::Record> AssetDatabase::RecordFromJson(const nlohmann::json& j)
    {
        if (!j.is_object())
        {
            return Result<Record>(ErrorCode::InvalidArgument, "Record must be an object");
        }

        Record r;
        if (j.contains("guid") && j["guid"].is_string()) r.Guid = j["guid"].get<std::string>();
        if (j.contains("key") && j["key"].is_string()) r.Key = j["key"].get<std::string>();
        if (j.contains("resolvedPath") && j["resolvedPath"].is_string()) r.ResolvedPath = j["resolvedPath"].get<std::string>();
        if (j.contains("type") && j["type"].is_string()) r.Type = AssetTypeFromString(j["type"].get<std::string>());
        if (j.contains("importerSettings") && j["importerSettings"].is_object()) r.ImporterSettings = j["importerSettings"];
        if (j.contains("deps") && j["deps"].is_array())
        {
            for (const auto& d : j["deps"])
            {
                if (d.is_string())
                {
                    r.Dependencies.push_back(d.get<std::string>());
                }
            }
        }
        if (j.contains("sourceSizeBytes") && j["sourceSizeBytes"].is_number_unsigned()) r.SourceSizeBytes = j["sourceSizeBytes"].get<uint64_t>();
        if (j.contains("sourceLastWriteTimeTicks") && j["sourceLastWriteTimeTicks"].is_number_integer()) r.SourceLastWriteTimeTicks = j["sourceLastWriteTimeTicks"].get<int64_t>();
        if (j.contains("importerSettingsHash64") && j["importerSettingsHash64"].is_number_unsigned()) r.ImporterSettingsHash64 = j["importerSettingsHash64"].get<uint64_t>();
        if (j.contains("importerVersion") && j["importerVersion"].is_number_unsigned()) r.ImporterVersion = j["importerVersion"].get<uint32_t>();
        if (j.contains("sourceKind") && j["sourceKind"].is_string()) r.SourceKind = AssetSourceKindFromString(j["sourceKind"].get<std::string>());

        if (r.Guid.empty() || r.Key.empty())
        {
            return Result<Record>(ErrorCode::InvalidArgument, "Record missing guid/key");
        }

        return r;
    }

    void AssetDatabase::RebuildDependentsIndexLocked()
    {
        m_DependentsByGuid.clear();
        m_DependentsByGuid.reserve(m_ByGuid.size());

        for (const auto& [guid, record] : m_ByGuid)
        {
            (void)guid;
            for (const auto& dep : record.Dependencies)
            {
                if (dep.empty())
                {
                    continue;
                }
                m_DependentsByGuid[dep].push_back(record.Guid);
            }
        }
    }
}

