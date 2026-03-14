#define DOCTEST_CONFIG_WITH_VARIADIC_MACROS
#include <doctest/doctest.h>
#include "Assets/AssetDatabase.h"
#include "Assets/AssetImportPipeline.h"
#include "Assets/AssetRegistryCache.h"
#include "Assets/AssetManager.h"
#include "Assets/TextureAsset.h"
#include "Project/ProjectManager.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <unordered_set>

namespace
{
    std::filesystem::path MakeTempProjectRoot(const std::string& folderName)
    {
        return std::filesystem::temp_directory_path() / "LimitlessRemasterTests" / folderName;
    }
}

TEST_SUITE("Asset System")
{
    TEST_CASE("AssetManager cache stats are initially zero")
    {
        Limitless::Assets::AssetManager::ClearCaches();

        size_t keyCount = 999;
        size_t guidCount = 999;
        Limitless::Assets::AssetManager::GetCacheStats(keyCount, guidCount);

        CHECK(keyCount == 0);
        CHECK(guidCount == 0);
    }

    TEST_CASE("AssetManager GetCachedByKey returns nullptr for empty key")
    {
        auto result = Limitless::Assets::AssetManager::GetCachedByKey("");
        CHECK(result == nullptr);
    }

    TEST_CASE("AssetManager GetCachedByKey returns nullptr for non-existent key")
    {
        auto result = Limitless::Assets::AssetManager::GetCachedByKey("Assets/NonExistent/Asset.xyz");
        CHECK(result == nullptr);
    }

    TEST_CASE("AssetManager GetCachedByGuid returns nullptr for empty guid")
    {
        auto result = Limitless::Assets::AssetManager::GetCachedByGuid("");
        CHECK(result == nullptr);
    }

    TEST_CASE("AssetManager GetByGuid returns nullptr for empty guid")
    {
        auto result = Limitless::Assets::AssetManager::GetByGuid<Limitless::Assets::TextureAsset>("");
        CHECK(result == nullptr);
    }

    TEST_CASE("AssetManager ClearCaches does not crash")
    {
        CHECK_NOTHROW(Limitless::Assets::AssetManager::ClearCaches());
    }

    TEST_CASE("AssetManager GetCacheStats after ClearCaches reports zero")
    {
        Limitless::Assets::AssetManager::ClearCaches();

        size_t keyCount = 999;
        size_t guidCount = 999;
        Limitless::Assets::AssetManager::GetCacheStats(keyCount, guidCount);

        CHECK(keyCount == 0);
        CHECK(guidCount == 0);
    }

    TEST_CASE("AssetDatabase rebuilds binary registry cache and falls back to JSON when cache is invalid")
    {
        using namespace Limitless;

        const std::filesystem::path projectRoot = MakeTempProjectRoot("AssetRegistryCacheFallback");
        std::error_code errorCode;
        Project::ProjectManager::GetInstance().CloseProject();
        std::filesystem::remove_all(projectRoot, errorCode);
        errorCode.clear();

        const auto createProjectResult = Project::ProjectManager::GetInstance().CreateProjectRoot(projectRoot, "AssetRegistryCacheFallback");
        REQUIRE(createProjectResult.IsSuccess());

        const std::filesystem::path shaderPath = projectRoot / "Assets" / "Shaders" / "CacheFallback.glsl";
        std::filesystem::create_directories(shaderPath.parent_path(), errorCode);
        REQUIRE_FALSE(errorCode);
        {
            std::ofstream shaderStream(shaderPath, std::ios::out | std::ios::binary | std::ios::trunc);
            REQUIRE(shaderStream.is_open());
            shaderStream << "#version 330 core\nvoid main() {}\n";
        }

        const std::string assetKey = "Assets/Shaders/CacheFallback.glsl";
        const auto importResult = Assets::AssetDatabase::GetInstance().ImportOrUpdate(assetKey, Assets::AssetType::Shader);
        REQUIRE(importResult.IsSuccess());

        const std::filesystem::path databasePath = projectRoot / "Build" / "AssetDatabase.json";
        const std::filesystem::path cachePath = Assets::AssetRegistryCache::GetCacheFilePath(databasePath);
        CHECK(std::filesystem::exists(databasePath));
        CHECK(std::filesystem::exists(cachePath));

        const uint64_t databaseSizeBytes = std::filesystem::file_size(databasePath);
        const int64_t databaseLastWriteTicks = static_cast<int64_t>(std::filesystem::last_write_time(databasePath).time_since_epoch().count());
        const auto cacheLoadResult = Assets::AssetRegistryCache::LoadFromFile(cachePath, 2u, databaseSizeBytes, databaseLastWriteTicks);
        REQUIRE(cacheLoadResult.IsSuccess());
        REQUIRE(cacheLoadResult.GetValue().Entries.size() == 1);
        CHECK(cacheLoadResult.GetValue().Entries.front().Key == assetKey);
        CHECK(cacheLoadResult.GetValue().Entries.front().Guid == importResult.GetValue().Guid);

        {
            std::ofstream cacheStream(cachePath, std::ios::out | std::ios::binary | std::ios::trunc);
            REQUIRE(cacheStream.is_open());
            cacheStream << "corrupted-cache";
        }

        Assets::AssetDatabase::GetInstance().Reset();
        const auto reloadedRecord = Assets::AssetDatabase::GetInstance().FindByKey(assetKey);
        REQUIRE(reloadedRecord.IsSuccess());
        CHECK(reloadedRecord.GetValue().Guid == importResult.GetValue().Guid);
        CHECK(std::filesystem::exists(cachePath));

        const uint64_t rebuiltDatabaseSizeBytes = std::filesystem::file_size(databasePath);
        const int64_t rebuiltDatabaseLastWriteTicks = static_cast<int64_t>(std::filesystem::last_write_time(databasePath).time_since_epoch().count());
        const auto rebuiltCacheLoadResult = Assets::AssetRegistryCache::LoadFromFile(cachePath, 2u, rebuiltDatabaseSizeBytes, rebuiltDatabaseLastWriteTicks);
        REQUIRE(rebuiltCacheLoadResult.IsSuccess());
        REQUIRE(rebuiltCacheLoadResult.GetValue().Entries.size() == 1);
        CHECK(rebuiltCacheLoadResult.GetValue().Entries.front().Key == assetKey);

        Project::ProjectManager::GetInstance().CloseProject();
        std::filesystem::remove_all(projectRoot, errorCode);
    }

    TEST_CASE("AssetDatabase persists explicit importer versions")
    {
        using namespace Limitless;

        const std::filesystem::path projectRoot = MakeTempProjectRoot("AssetImporterVersionPersistence");
        std::error_code errorCode;
        Project::ProjectManager::GetInstance().CloseProject();
        std::filesystem::remove_all(projectRoot, errorCode);
        errorCode.clear();

        const auto createProjectResult = Project::ProjectManager::GetInstance().CreateProjectRoot(projectRoot, "AssetImporterVersionPersistence");
        REQUIRE(createProjectResult.IsSuccess());

        const std::filesystem::path shaderPath = projectRoot / "Assets" / "Shaders" / "ImporterVersion.glsl";
        std::filesystem::create_directories(shaderPath.parent_path(), errorCode);
        REQUIRE_FALSE(errorCode);
        {
            std::ofstream shaderStream(shaderPath, std::ios::out | std::ios::binary | std::ios::trunc);
            REQUIRE(shaderStream.is_open());
            shaderStream << "#version 330 core\nvoid main() {}\n";
        }

        const std::string assetKey = "Assets/Shaders/ImporterVersion.glsl";
        const auto importResult = Assets::AssetDatabase::GetInstance().ImportOrUpdate(assetKey, Assets::AssetType::Shader, nlohmann::json::object(), 7u);
        REQUIRE(importResult.IsSuccess());
        CHECK(importResult.GetValue().ImporterVersion == 7u);

        const auto reloadedRecord = Assets::AssetDatabase::GetInstance().FindByKey(assetKey);
        REQUIRE(reloadedRecord.IsSuccess());
        CHECK(reloadedRecord.GetValue().ImporterVersion == 7u);

        Project::ProjectManager::GetInstance().CloseProject();
        std::filesystem::remove_all(projectRoot, errorCode);
    }

    TEST_CASE("AssetImportPipeline reports self dependency missing dependency and cycles")
    {
        using namespace Limitless;

        const std::filesystem::path projectRoot = MakeTempProjectRoot("AssetDependencyValidation");
        std::error_code errorCode;
        Project::ProjectManager::GetInstance().CloseProject();
        std::filesystem::remove_all(projectRoot, errorCode);
        errorCode.clear();

        const auto createProjectResult = Project::ProjectManager::GetInstance().CreateProjectRoot(projectRoot, "AssetDependencyValidation");
        REQUIRE(createProjectResult.IsSuccess());

        const std::filesystem::path shaderAPath = projectRoot / "Assets" / "Shaders" / "ValidationA.glsl";
        const std::filesystem::path shaderBPath = projectRoot / "Assets" / "Shaders" / "ValidationB.glsl";
        std::filesystem::create_directories(shaderAPath.parent_path(), errorCode);
        REQUIRE_FALSE(errorCode);
        {
            std::ofstream shaderAStream(shaderAPath, std::ios::out | std::ios::binary | std::ios::trunc);
            REQUIRE(shaderAStream.is_open());
            shaderAStream << "#version 330 core\nvoid main() {}\n";
        }
        {
            std::ofstream shaderBStream(shaderBPath, std::ios::out | std::ios::binary | std::ios::trunc);
            REQUIRE(shaderBStream.is_open());
            shaderBStream << "#version 330 core\nvoid main() {}\n";
        }

        const auto importA = Assets::AssetDatabase::GetInstance().ImportOrUpdate("Assets/Shaders/ValidationA.glsl", Assets::AssetType::Shader, nlohmann::json::object(), 3u);
        const auto importB = Assets::AssetDatabase::GetInstance().ImportOrUpdate("Assets/Shaders/ValidationB.glsl", Assets::AssetType::Shader, nlohmann::json::object(), 3u);
        REQUIRE(importA.IsSuccess());
        REQUIRE(importB.IsSuccess());

        const std::string missingGuid = "missing-guid-record";
        const auto setDepsA = Assets::AssetDatabase::GetInstance().SetDependencies(
            importA.GetValue().Guid,
            { importA.GetValue().Guid, missingGuid, importB.GetValue().Guid });
        REQUIRE(setDepsA.IsSuccess());
        const auto setDepsB = Assets::AssetDatabase::GetInstance().SetDependencies(
            importB.GetValue().Guid,
            { importA.GetValue().Guid });
        REQUIRE(setDepsB.IsSuccess());

        const auto validationResult = Assets::AssetImportPipeline::ValidateAssetDatabase();
        REQUIRE(validationResult.IsSuccess());

        const auto& issues = validationResult.GetValue();
        const bool hasSelfDependency = std::any_of(issues.begin(), issues.end(), [&](const Assets::AssetDatabaseValidationIssue& issue)
        {
            return issue.IssueType == Assets::AssetDatabaseValidationIssue::Type::SelfDependency &&
                   issue.Guid == importA.GetValue().Guid;
        });
        const bool hasMissingDependency = std::any_of(issues.begin(), issues.end(), [&](const Assets::AssetDatabaseValidationIssue& issue)
        {
            return issue.IssueType == Assets::AssetDatabaseValidationIssue::Type::MissingDependencyRecord &&
                   issue.Guid == importA.GetValue().Guid;
        });
        const bool hasDependencyCycle = std::any_of(issues.begin(), issues.end(), [&](const Assets::AssetDatabaseValidationIssue& issue)
        {
            return issue.IssueType == Assets::AssetDatabaseValidationIssue::Type::DependencyCycle;
        });

        CHECK(hasSelfDependency);
        CHECK(hasMissingDependency);
        CHECK(hasDependencyCycle);

        Project::ProjectManager::GetInstance().CloseProject();
        std::filesystem::remove_all(projectRoot, errorCode);
    }

    TEST_CASE("AssetDatabase CommitRecordBatch commits multiple records with single save")
    {
        using namespace Limitless;

        const std::filesystem::path projectRoot = MakeTempProjectRoot("CommitRecordBatch");
        std::error_code errorCode;
        Project::ProjectManager::GetInstance().CloseProject();
        std::filesystem::remove_all(projectRoot, errorCode);
        errorCode.clear();

        const auto createProjectResult = Project::ProjectManager::GetInstance().CreateProjectRoot(projectRoot, "CommitRecordBatch");
        REQUIRE(createProjectResult.IsSuccess());

        const std::filesystem::path shadersDir = projectRoot / "Assets" / "Shaders";
        std::filesystem::create_directories(shadersDir, errorCode);
        REQUIRE_FALSE(errorCode);

        constexpr size_t kAssetCount = 5;
        std::vector<std::string> keys;
        keys.reserve(kAssetCount);
        for (size_t i = 0; i < kAssetCount; ++i)
        {
            const std::string name = "BatchShader" + std::to_string(i) + ".glsl";
            const std::filesystem::path path = shadersDir / name;
            std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
            REQUIRE(out.is_open());
            out << "#version 330 core\nvoid main() {}\n";
            keys.push_back("Assets/Shaders/" + name);
        }

        // Import individually first to establish GUIDs.
        for (const auto& key : keys)
        {
            const auto result = Assets::AssetDatabase::GetInstance().ImportOrUpdate(key, Assets::AssetType::Shader, nlohmann::json::object(), 1u);
            REQUIRE(result.IsSuccess());
        }

        const uint64_t revisionBefore = Assets::AssetDatabase::GetInstance().GetRevision();

        // Build records for batch commit with updated importer version.
        std::vector<Assets::AssetDatabase::Record> records;
        records.reserve(kAssetCount);
        for (const auto& key : keys)
        {
            const auto existing = Assets::AssetDatabase::GetInstance().FindByKey(key);
            REQUIRE(existing.IsSuccess());
            Assets::AssetDatabase::Record r = existing.GetValue();
            r.ImporterVersion = 5u;
            records.push_back(std::move(r));
        }

        const auto committed = Assets::AssetDatabase::GetInstance().CommitRecordBatch(records);
        CHECK(committed.size() == kAssetCount);

        // Revision should have incremented exactly once (single save).
        const uint64_t revisionAfter = Assets::AssetDatabase::GetInstance().GetRevision();
        CHECK(revisionAfter == revisionBefore + 1);

        // Verify all records have the new version.
        for (const auto& key : keys)
        {
            const auto record = Assets::AssetDatabase::GetInstance().FindByKey(key);
            REQUIRE(record.IsSuccess());
            CHECK(record.GetValue().ImporterVersion == 5u);
        }

        // Verify no duplicate GUIDs.
        std::unordered_set<std::string> guids;
        for (const auto& r : committed)
        {
            CHECK(guids.insert(r.Guid).second);
        }

        Project::ProjectManager::GetInstance().CloseProject();
        std::filesystem::remove_all(projectRoot, errorCode);
    }

    TEST_CASE("AssetImportPipeline ReimportAll produces deterministic results in sequential and parallel modes")
    {
        using namespace Limitless;

        const std::filesystem::path projectRoot = MakeTempProjectRoot("ParallelImportDeterminism");
        std::error_code errorCode;
        Project::ProjectManager::GetInstance().CloseProject();
        std::filesystem::remove_all(projectRoot, errorCode);
        errorCode.clear();

        const auto createProjectResult = Project::ProjectManager::GetInstance().CreateProjectRoot(projectRoot, "ParallelImportDeterminism");
        REQUIRE(createProjectResult.IsSuccess());

        const std::filesystem::path shadersDir = projectRoot / "Assets" / "Shaders";
        std::filesystem::create_directories(shadersDir, errorCode);
        REQUIRE_FALSE(errorCode);

        constexpr size_t kAssetCount = 8;
        for (size_t i = 0; i < kAssetCount; ++i)
        {
            const std::string name = "DetShader" + std::to_string(i) + ".glsl";
            std::ofstream out(shadersDir / name, std::ios::out | std::ios::binary | std::ios::trunc);
            REQUIRE(out.is_open());
            out << "#version 330 core\nvoid main() { /* " << i << " */ }\n";
        }

        // Run 1: sequential (ForceSequential = true)
        Assets::AssetDatabase::GetInstance().Reset();
        Assets::ParallelImportConfig seqConfig;
        seqConfig.ForceSequential = true;
        const auto seqResult = Assets::AssetImportPipeline::ReimportAll(false, seqConfig);
        REQUIRE(seqResult.IsSuccess());
        const auto& seqStats = seqResult.GetValue();
        auto seqKeys = seqStats.ImportedKeys;
        std::sort(seqKeys.begin(), seqKeys.end());

        // Run 2: parallel (ForceSequential = false, default)
        Assets::AssetDatabase::GetInstance().Reset();
        Assets::ParallelImportConfig parConfig;
        parConfig.ForceSequential = false;
        const auto parResult = Assets::AssetImportPipeline::ReimportAll(false, parConfig);
        REQUIRE(parResult.IsSuccess());
        const auto& parStats = parResult.GetValue();
        auto parKeys = parStats.ImportedKeys;
        std::sort(parKeys.begin(), parKeys.end());

        // Both runs should import the same set of keys.
        CHECK(seqStats.DiscoveredFiles == parStats.DiscoveredFiles);
        CHECK(seqStats.Imported == parStats.Imported);
        CHECK(seqStats.SkippedUpToDate == parStats.SkippedUpToDate);
        CHECK(seqStats.Errors == parStats.Errors);
        CHECK(seqKeys == parKeys);

        // Verify no duplicate GUIDs across all records.
        const auto allRecords = Assets::AssetDatabase::GetInstance().GetAllRecords();
        std::unordered_set<std::string> guids;
        for (const auto& r : allRecords)
        {
            if (!r.Guid.empty())
            {
                CHECK(guids.insert(r.Guid).second);
            }
        }

        Project::ProjectManager::GetInstance().CloseProject();
        std::filesystem::remove_all(projectRoot, errorCode);
    }

    TEST_CASE("AssetImportPipeline ReimportChanged skips up-to-date assets after initial import")
    {
        using namespace Limitless;

        const std::filesystem::path projectRoot = MakeTempProjectRoot("ParallelReimportChanged");
        std::error_code errorCode;
        Project::ProjectManager::GetInstance().CloseProject();
        std::filesystem::remove_all(projectRoot, errorCode);
        errorCode.clear();

        const auto createProjectResult = Project::ProjectManager::GetInstance().CreateProjectRoot(projectRoot, "ParallelReimportChanged");
        REQUIRE(createProjectResult.IsSuccess());

        const std::filesystem::path shadersDir = projectRoot / "Assets" / "Shaders";
        std::filesystem::create_directories(shadersDir, errorCode);
        REQUIRE_FALSE(errorCode);

        constexpr size_t kAssetCount = 4;
        for (size_t i = 0; i < kAssetCount; ++i)
        {
            const std::string name = "ChangedShader" + std::to_string(i) + ".glsl";
            std::ofstream out(shadersDir / name, std::ios::out | std::ios::binary | std::ios::trunc);
            REQUIRE(out.is_open());
            out << "#version 330 core\nvoid main() {}\n";
        }

        // First import: all assets should be imported.
        const auto firstResult = Assets::AssetImportPipeline::ReimportAll(false);
        REQUIRE(firstResult.IsSuccess());
        CHECK(firstResult.GetValue().Imported == kAssetCount);

        // Second import (changed only): all should be skipped.
        const auto secondResult = Assets::AssetImportPipeline::ReimportChanged(false);
        REQUIRE(secondResult.IsSuccess());
        CHECK(secondResult.GetValue().Imported == 0);
        CHECK(secondResult.GetValue().SkippedUpToDate == kAssetCount);

        Project::ProjectManager::GetInstance().CloseProject();
        std::filesystem::remove_all(projectRoot, errorCode);
    }
}
