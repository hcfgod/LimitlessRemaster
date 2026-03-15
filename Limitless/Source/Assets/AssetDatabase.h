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
    // -------------------------------------------------------------------------
    // AssetSourceKind
    // Distinguishes how a record entered the database.
    // -------------------------------------------------------------------------
    enum class AssetSourceKind : uint8_t
    {
        FileBacked = 0, // Normal asset backed by a file under Assets/
        Generated  = 1  // Produced by an engine system (atlas, lightmap, etc.)
    };

    [[nodiscard]] inline const char* ToString(AssetSourceKind kind)
    {
        switch (kind)
        {
            case AssetSourceKind::FileBacked: return "FileBacked";
            case AssetSourceKind::Generated:  return "Generated";
            default:                          return "FileBacked";
        }
    }

    [[nodiscard]] inline AssetSourceKind AssetSourceKindFromString(const std::string& s)
    {
        if (s == "Generated") return AssetSourceKind::Generated;
        return AssetSourceKind::FileBacked;
    }

    class AssetDatabase final
    {
    public:
        struct Record
        {
            Record() = default;
            Record(const Record&) = default;
            Record(Record&&) noexcept = default;
            Record& operator=(const Record&) = default;
            Record& operator=(Record&&) noexcept = default;

            std::string Guid{};
            std::string Key{}; // Unity-style key (example: "Assets/Textures/X.png")
            std::string ResolvedPath{}; // absolute path used for I/O (empty for generated assets)
            AssetType Type{AssetType::Unknown};
            AssetSourceKind SourceKind{AssetSourceKind::FileBacked};
            nlohmann::json ImporterSettings{nlohmann::json::object()};
            std::vector<std::string> Dependencies{}; // GUIDs

            // Import fingerprint (tooling/editor):
            // Used to support incremental reimport/cooking decisions.
            uint64_t SourceSizeBytes{0};
            int64_t SourceLastWriteTimeTicks{0};
            uint64_t ImporterSettingsHash64{0};
            uint32_t ImporterVersion{1};

            [[nodiscard]] bool IsGenerated() const { return SourceKind == AssetSourceKind::Generated; }
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
        Result<Record> ImportOrUpdate(const std::string& key,
                                      AssetType type,
                                      const nlohmann::json& importerSettings = nlohmann::json::object(),
                                      uint32_t importerVersion = 0u);

        // Update dependency list for a GUID:
        // - stored in database
        // - written into the asset `.meta` file ("deps")
        Result<void> SetDependencies(const std::string& guid, const std::vector<std::string>& dependencies);

        // Find all assets that depend on `guid` (reverse dependency lookup).
        std::vector<Record> GetDependentsOf(const std::string& guid);

        // Remove records (tooling only). This does NOT delete files on disk.
        Result<void> RemoveByGuid(const std::string& guid);
        Result<void> RemoveByKey(const std::string& key);

        // Register a generated (virtual) asset.
        // Generated assets have no source file; they are produced by engine systems
        // (e.g. sprite atlas, lightmap, navmesh).  The caller supplies a stable GUID
        // and a virtual key.  If a record with the same GUID already exists it is
        // updated in place; otherwise a new record is created.
        Result<Record> RegisterGeneratedAsset(const std::string& guid,
                                              const std::string& virtualKey,
                                              AssetType type,
                                              const nlohmann::json& importerSettings = nlohmann::json::object(),
                                              uint32_t importerVersion = 0u);

        // Batch-commit prepared records with a single save-to-disk at the end.
        // Records must have Guid, Key, ResolvedPath, Type, and fingerprint fields populated.
        // The same merge logic as ImportOrUpdate is applied per record (GUID changes,
        // dependency preservation, stale alias cleanup) but the expensive disk save
        // happens only once after all records are committed.
        // Returns the list of successfully committed records.
        std::vector<Record> CommitRecordBatch(std::vector<Record>& records);

        // Snapshot all records (for tooling/debug).
        std::vector<Record> GetAllRecords();

        // For debugging/telemetry.
        size_t GetRecordCount() const;
        // Monotonic counter that changes whenever database contents are reloaded/mutated.
        uint64_t GetRevision() const;

        struct CacheTelemetry
        {
            uint64_t CacheHits = 0;
            uint64_t CacheMisses = 0;
        };
        CacheTelemetry GetCacheTelemetry() const;

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

        // Cache load telemetry.
        uint64_t m_CacheHits = 0;
        uint64_t m_CacheMisses = 0;

        // Reverse dependency index:
        // depGuid -> list of guids that depend on depGuid
        std::unordered_map<std::string, std::vector<std::string>> m_DependentsByGuid;
    };
}

