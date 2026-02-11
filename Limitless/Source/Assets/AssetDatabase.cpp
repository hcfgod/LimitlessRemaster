#include "Assets/AssetDatabase.h"

#include "Assets/AssetBundle.h"
#include "Assets/AssetPaths.h"
#include "Assets/AssetUtils.h"

#include "Core/Debug/Log.h"

#include <fstream>

namespace Limitless::Assets
{
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

    Result<AssetDatabase::Record> AssetDatabase::ImportOrUpdate(const std::string& key, AssetType type, const nlohmann::json& importerSettings)
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

        const auto guidResult = LoadOrCreateGuid(resolvedPath.string(), {{"key", key}, {"type", ToString(type)}});
        if (guidResult.IsFailure())
        {
            return Result<Record>(guidResult.GetError());
        }

        Record record;
        record.Guid = guidResult.GetValue();
        record.Key = key;
        record.ResolvedPath = resolvedPath.string();
        record.Type = type;
        record.ImporterSettings = importerSettings;

        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            // Preserve existing dependencies if present.
            if (auto it = m_ByGuid.find(record.Guid); it != m_ByGuid.end())
            {
                record.Dependencies = it->second.Dependencies;

                // Preserve importer settings unless explicitly overridden.
                // This is critical because many tooling paths (bundle discovery, watchers, etc.)
                // may call ImportOrUpdate with an empty object just to ensure a GUID exists.
                if (record.ImporterSettings.is_object() && record.ImporterSettings.empty())
                {
                    record.ImporterSettings = it->second.ImporterSettings;
                }
            }

            m_ByGuid[record.Guid] = record;
            m_GuidByKey[record.Key] = record.Guid;

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

            it->second.Dependencies = dependencies;
            resolvedPath = it->second.ResolvedPath;

            const auto saveResult = SaveToDiskLocked();
            if (saveResult.IsFailure())
            {
                LT_CORE_WARN("AssetDatabase: failed to save database: {}", saveResult.GetError().GetErrorMessage());
            }
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

        for (const auto& [id, record] : m_ByGuid)
        {
            for (const auto& dep : record.Dependencies)
            {
                if (dep == guid)
                {
                    out.push_back(record);
                    break;
                }
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

    size_t AssetDatabase::GetRecordCount() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_ByGuid.size();
    }

    Result<std::filesystem::path> AssetDatabase::GetDatabaseFilePathLocked()
    {
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

        if (!std::filesystem::exists(dbPath))
        {
            return Result<void>(ErrorCode::FileNotFound, "AssetDatabase file not found");
        }

        try
        {
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

                m_GuidByKey[r.Key] = r.Guid;
                m_ByGuid[r.Guid] = std::move(r);
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
            root["version"] = 1;
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

        if (r.Guid.empty() || r.Key.empty())
        {
            return Result<Record>(ErrorCode::InvalidArgument, "Record missing guid/key");
        }

        return r;
    }
}

