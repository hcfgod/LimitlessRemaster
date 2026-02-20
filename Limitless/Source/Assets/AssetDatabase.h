#pragma once

#include "Assets/AssetTypes.h"
#include "Core/Error.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Limitless::Assets
{
    // -----------------------------------------------------------------------------
    // AssetDatabase (P0)
    // Persistent manifest that maps:
    // - GUID ↔ key/path
    // - GUID ↔ asset type
    // - importer settings (JSON)
    // - dependencies (GUID list)
    //
    // Stored as a single JSON file (currently `Build/AssetDatabase.json` under project root).
    //
    // Threading:
    // - Public methods are thread-safe via a mutex.
    // - Import operations are small and mostly metadata I/O.
    // -----------------------------------------------------------------------------
    class AssetDatabase final
    {
    public:
        struct Record
        {
            std::string Guid;
            std::string Key; // Unity-style key (example: "Assets/Textures/X.png")
            std::string ResolvedPath; // absolute path used for I/O
            AssetType Type = AssetType::Unknown;
            nlohmann::json ImporterSettings = nlohmann::json::object();
            std::vector<std::string> Dependencies; // GUIDs

            // Import fingerprint (tooling/editor):
            // Used to support incremental reimport/cooking decisions.
            uint64_t SourceSizeBytes = 0;
            int64_t SourceLastWriteTimeTicks = 0;
            uint64_t ImporterSettingsHash64 = 0;
            uint32_t ImporterVersion = 1;
        };

        static AssetDatabase& GetInstance();

        // Ensure the database has been loaded from disk (lazy).
        void EnsureLoaded();
        // Reset in-memory state so the next query reloads from current project root.
        void Reset();

        // Find a record by GUID/key.
        Result<Record> FindByGuid(const std::string& guid);
        Result<Record> FindByKey(const std::string& key);

        // Import (or reimport metadata) for a key/type pair.
        // - resolves key to a filesystem path
        // - ensures `.meta` GUID exists next to the real file
        // - upserts record and persists the database file
        Result<Record> ImportOrUpdate(const std::string& key, AssetType type, const nlohmann::json& importerSettings = nlohmann::json::object());

        // Update dependency list for a GUID:
        // - stored in database
        // - written into the asset `.meta` file ("deps")
        Result<void> SetDependencies(const std::string& guid, const std::vector<std::string>& dependencies);

        // Find all assets that depend on `guid` (reverse dependency lookup).
        std::vector<Record> GetDependentsOf(const std::string& guid);

        // Remove records (tooling only). This does NOT delete files on disk.
        Result<void> RemoveByGuid(const std::string& guid);
        Result<void> RemoveByKey(const std::string& key);

        // Snapshot all records (for tooling/debug).
        std::vector<Record> GetAllRecords();

        // For debugging/telemetry.
        size_t GetRecordCount() const;
        // Monotonic counter that changes whenever database contents are reloaded/mutated.
        uint64_t GetRevision() const;

    private:
        AssetDatabase() = default;

        Result<std::filesystem::path> GetDatabaseFilePathLocked();
        Result<void> LoadFromDiskLocked();
        Result<void> SaveToDiskLocked();

        static nlohmann::json RecordToJson(const Record& r);
        static Result<Record> RecordFromJson(const nlohmann::json& j);

        void RebuildDependentsIndexLocked();

    private:
        mutable std::mutex m_Mutex;
        bool m_Loaded = false;
        uint64_t m_Revision = 1;

        // GUID -> Record
        std::unordered_map<std::string, Record> m_ByGuid;
        // Key -> GUID
        std::unordered_map<std::string, std::string> m_GuidByKey;

        // Reverse dependency index:
        // depGuid -> list of guids that depend on depGuid
        std::unordered_map<std::string, std::vector<std::string>> m_DependentsByGuid;
    };
}

