#define DOCTEST_CONFIG_WITH_VARIADIC_MACROS
#include <doctest/doctest.h>
#include "Assets/AssetDatabase.h"
#include "Assets/GeneratedAssetRuntimeRegistry.h"
#include "Assets/AssetImportPipeline.h"
#include "Assets/AssetRegistryCache.h"
#include "Assets/AssetManager.h"
#include "Assets/InputActionsAssetResource.h"
#include "Assets/TextureAsset.h"
#include "Core/Input/InputAction.h"
#include "Project/ProjectManager.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>
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

    TEST_CASE("Generated asset can be registered and looked up by GUID and key")
    {
        using namespace Limitless;

        const std::filesystem::path projectRoot = MakeTempProjectRoot("GeneratedAssetRegister");
        std::error_code errorCode;
        Project::ProjectManager::GetInstance().CloseProject();
        std::filesystem::remove_all(projectRoot, errorCode);
        errorCode.clear();

        const auto createProjectResult = Project::ProjectManager::GetInstance().CreateProjectRoot(projectRoot, "GeneratedAssetRegister");
        REQUIRE(createProjectResult.IsSuccess());

        const std::string guid = "generated-atlas-guid-001";
        const std::string virtualKey = "Generated/Atlases/SpriteAtlas0";
        const auto regResult = Assets::AssetDatabase::GetInstance().RegisterGeneratedAsset(
            guid, virtualKey, Assets::AssetType::Texture2D);
        REQUIRE(regResult.IsSuccess());

        const auto& record = regResult.GetValue();
        CHECK(record.Guid == guid);
        CHECK(record.Key == virtualKey);
        CHECK(record.ResolvedPath.empty());
        CHECK(record.Type == Assets::AssetType::Texture2D);
        CHECK(record.IsGenerated());
        CHECK(record.SourceKind == Assets::AssetSourceKind::Generated);

        const auto byGuid = Assets::AssetDatabase::GetInstance().FindByGuid(guid);
        REQUIRE(byGuid.IsSuccess());
        CHECK(byGuid.GetValue().Key == virtualKey);
        CHECK(byGuid.GetValue().IsGenerated());

        const auto byKey = Assets::AssetDatabase::GetInstance().FindByKey(virtualKey);
        REQUIRE(byKey.IsSuccess());
        CHECK(byKey.GetValue().Guid == guid);
        CHECK(byKey.GetValue().IsGenerated());

        Project::ProjectManager::GetInstance().CloseProject();
        std::filesystem::remove_all(projectRoot, errorCode);
    }

    TEST_CASE("Generated asset survives database reset and reload from disk")
    {
        using namespace Limitless;

        const std::filesystem::path projectRoot = MakeTempProjectRoot("GeneratedAssetPersistence");
        std::error_code errorCode;
        Project::ProjectManager::GetInstance().CloseProject();
        std::filesystem::remove_all(projectRoot, errorCode);
        errorCode.clear();

        const auto createProjectResult = Project::ProjectManager::GetInstance().CreateProjectRoot(projectRoot, "GeneratedAssetPersistence");
        REQUIRE(createProjectResult.IsSuccess());

        const std::string guid = "generated-persist-guid-002";
        const std::string virtualKey = "Generated/Lightmaps/LightmapA";
        const auto regResult = Assets::AssetDatabase::GetInstance().RegisterGeneratedAsset(
            guid, virtualKey, Assets::AssetType::Texture2D);
        REQUIRE(regResult.IsSuccess());

        Assets::AssetDatabase::GetInstance().Reset();

        const auto reloaded = Assets::AssetDatabase::GetInstance().FindByGuid(guid);
        REQUIRE(reloaded.IsSuccess());
        CHECK(reloaded.GetValue().Key == virtualKey);
        CHECK(reloaded.GetValue().IsGenerated());
        CHECK(reloaded.GetValue().ResolvedPath.empty());

        Project::ProjectManager::GetInstance().CloseProject();
        std::filesystem::remove_all(projectRoot, errorCode);
    }

    TEST_CASE("Generated asset supports dependency tracking without .meta files")
    {
        using namespace Limitless;

        const std::filesystem::path projectRoot = MakeTempProjectRoot("GeneratedAssetDeps");
        std::error_code errorCode;
        Project::ProjectManager::GetInstance().CloseProject();
        std::filesystem::remove_all(projectRoot, errorCode);
        errorCode.clear();

        const auto createProjectResult = Project::ProjectManager::GetInstance().CreateProjectRoot(projectRoot, "GeneratedAssetDeps");
        REQUIRE(createProjectResult.IsSuccess());

        const std::filesystem::path texPath = projectRoot / "Assets" / "Textures" / "Source.png";
        std::filesystem::create_directories(texPath.parent_path(), errorCode);
        REQUIRE_FALSE(errorCode);
        {
            std::ofstream out(texPath, std::ios::out | std::ios::binary | std::ios::trunc);
            REQUIRE(out.is_open());
            out << "fake-png-data";
        }
        const auto texImport = Assets::AssetDatabase::GetInstance().ImportOrUpdate(
            "Assets/Textures/Source.png", Assets::AssetType::Texture2D);
        REQUIRE(texImport.IsSuccess());

        const std::string genGuid = "generated-deps-guid-003";
        const std::string genKey = "Generated/Atlases/AtlasWithDeps";
        const auto genResult = Assets::AssetDatabase::GetInstance().RegisterGeneratedAsset(
            genGuid, genKey, Assets::AssetType::Texture2D);
        REQUIRE(genResult.IsSuccess());

        const auto setDepsResult = Assets::AssetDatabase::GetInstance().SetDependencies(
            genGuid, { texImport.GetValue().Guid });
        REQUIRE(setDepsResult.IsSuccess());

        const auto record = Assets::AssetDatabase::GetInstance().FindByGuid(genGuid);
        REQUIRE(record.IsSuccess());
        REQUIRE(record.GetValue().Dependencies.size() == 1);
        CHECK(record.GetValue().Dependencies[0] == texImport.GetValue().Guid);

        const auto dependents = Assets::AssetDatabase::GetInstance().GetDependentsOf(texImport.GetValue().Guid);
        bool found = false;
        for (const auto& dep : dependents)
        {
            if (dep.Guid == genGuid) found = true;
        }
        CHECK(found);

        Project::ProjectManager::GetInstance().CloseProject();
        std::filesystem::remove_all(projectRoot, errorCode);
    }

    TEST_CASE("Generated asset is not discovered by reimport pipeline")
    {
        using namespace Limitless;

        const std::filesystem::path projectRoot = MakeTempProjectRoot("GeneratedAssetReimportSkip");
        std::error_code errorCode;
        Project::ProjectManager::GetInstance().CloseProject();
        std::filesystem::remove_all(projectRoot, errorCode);
        errorCode.clear();

        const auto createProjectResult = Project::ProjectManager::GetInstance().CreateProjectRoot(projectRoot, "GeneratedAssetReimportSkip");
        REQUIRE(createProjectResult.IsSuccess());

        std::filesystem::create_directories(projectRoot / "Assets", errorCode);
        REQUIRE_FALSE(errorCode);

        const std::string genGuid = "generated-reimport-skip-004";
        const std::string genKey = "Generated/Navmesh/NavmeshA";
        const auto genResult = Assets::AssetDatabase::GetInstance().RegisterGeneratedAsset(
            genGuid, genKey, Assets::AssetType::Mesh);
        REQUIRE(genResult.IsSuccess());

        const auto reimportResult = Assets::AssetImportPipeline::ReimportAll(false);
        REQUIRE(reimportResult.IsSuccess());

        const auto afterReimport = Assets::AssetDatabase::GetInstance().FindByGuid(genGuid);
        REQUIRE(afterReimport.IsSuccess());
        CHECK(afterReimport.GetValue().IsGenerated());
        CHECK(afterReimport.GetValue().Key == genKey);

        Project::ProjectManager::GetInstance().CloseProject();
        std::filesystem::remove_all(projectRoot, errorCode);
    }

    TEST_CASE("Generated asset can be removed by GUID and key")
    {
        using namespace Limitless;

        const std::filesystem::path projectRoot = MakeTempProjectRoot("GeneratedAssetRemove");
        std::error_code errorCode;
        Project::ProjectManager::GetInstance().CloseProject();
        std::filesystem::remove_all(projectRoot, errorCode);
        errorCode.clear();

        const auto createProjectResult = Project::ProjectManager::GetInstance().CreateProjectRoot(projectRoot, "GeneratedAssetRemove");
        REQUIRE(createProjectResult.IsSuccess());

        const std::string guid1 = "generated-remove-guid-005a";
        const std::string key1 = "Generated/Remove/AssetA";
        const std::string guid2 = "generated-remove-guid-005b";
        const std::string key2 = "Generated/Remove/AssetB";

        REQUIRE(Assets::AssetDatabase::GetInstance().RegisterGeneratedAsset(guid1, key1, Assets::AssetType::Texture2D).IsSuccess());
        REQUIRE(Assets::AssetDatabase::GetInstance().RegisterGeneratedAsset(guid2, key2, Assets::AssetType::Texture2D).IsSuccess());

        REQUIRE(Assets::AssetDatabase::GetInstance().RemoveByGuid(guid1).IsSuccess());
        CHECK(Assets::AssetDatabase::GetInstance().FindByGuid(guid1).IsFailure());
        CHECK(Assets::AssetDatabase::GetInstance().FindByKey(key1).IsFailure());

        REQUIRE(Assets::AssetDatabase::GetInstance().RemoveByKey(key2).IsSuccess());
        CHECK(Assets::AssetDatabase::GetInstance().FindByGuid(guid2).IsFailure());
        CHECK(Assets::AssetDatabase::GetInstance().FindByKey(key2).IsFailure());

        Project::ProjectManager::GetInstance().CloseProject();
        std::filesystem::remove_all(projectRoot, errorCode);
    }

    TEST_CASE("Generated asset validation does not report missing file")
    {
        using namespace Limitless;

        const std::filesystem::path projectRoot = MakeTempProjectRoot("GeneratedAssetValidation");
        std::error_code errorCode;
        Project::ProjectManager::GetInstance().CloseProject();
        std::filesystem::remove_all(projectRoot, errorCode);
        errorCode.clear();

        const auto createProjectResult = Project::ProjectManager::GetInstance().CreateProjectRoot(projectRoot, "GeneratedAssetValidation");
        REQUIRE(createProjectResult.IsSuccess());
        std::filesystem::create_directories(projectRoot / "Assets", errorCode);
        REQUIRE_FALSE(errorCode);

        const std::string genGuid = "generated-validate-guid-006";
        const std::string genKey = "Generated/Validate/TestAsset";
        REQUIRE(Assets::AssetDatabase::GetInstance().RegisterGeneratedAsset(
            genGuid, genKey, Assets::AssetType::Texture2D).IsSuccess());

        const auto validationResult = Assets::AssetImportPipeline::ValidateAssetDatabase();
        REQUIRE(validationResult.IsSuccess());

        const auto& issues = validationResult.GetValue();
        const bool hasMissingFile = std::any_of(issues.begin(), issues.end(), [&](const Assets::AssetDatabaseValidationIssue& issue)
        {
            return issue.IssueType == Assets::AssetDatabaseValidationIssue::Type::MissingFileForRecord &&
                   issue.Guid == genGuid;
        });
        CHECK_FALSE(hasMissingFile);

        Project::ProjectManager::GetInstance().CloseProject();
        std::filesystem::remove_all(projectRoot, errorCode);
    }

    TEST_CASE("File-backed assets remain unaffected by generated asset changes")
    {
        using namespace Limitless;

        const std::filesystem::path projectRoot = MakeTempProjectRoot("GeneratedAssetCoexist");
        std::error_code errorCode;
        Project::ProjectManager::GetInstance().CloseProject();
        std::filesystem::remove_all(projectRoot, errorCode);
        errorCode.clear();

        const auto createProjectResult = Project::ProjectManager::GetInstance().CreateProjectRoot(projectRoot, "GeneratedAssetCoexist");
        REQUIRE(createProjectResult.IsSuccess());

        const std::filesystem::path shaderPath = projectRoot / "Assets" / "Shaders" / "Coexist.glsl";
        std::filesystem::create_directories(shaderPath.parent_path(), errorCode);
        REQUIRE_FALSE(errorCode);
        {
            std::ofstream out(shaderPath, std::ios::out | std::ios::binary | std::ios::trunc);
            REQUIRE(out.is_open());
            out << "#version 330 core\nvoid main() {}\n";
        }
        const auto fileResult = Assets::AssetDatabase::GetInstance().ImportOrUpdate(
            "Assets/Shaders/Coexist.glsl", Assets::AssetType::Shader);
        REQUIRE(fileResult.IsSuccess());
        CHECK_FALSE(fileResult.GetValue().IsGenerated());
        CHECK(fileResult.GetValue().SourceKind == Assets::AssetSourceKind::FileBacked);

        const std::string genGuid = "generated-coexist-guid-007";
        const std::string genKey = "Generated/Coexist/Atlas";
        REQUIRE(Assets::AssetDatabase::GetInstance().RegisterGeneratedAsset(
            genGuid, genKey, Assets::AssetType::Texture2D).IsSuccess());

        const auto fileCheck = Assets::AssetDatabase::GetInstance().FindByKey("Assets/Shaders/Coexist.glsl");
        REQUIRE(fileCheck.IsSuccess());
        CHECK(fileCheck.GetValue().Guid == fileResult.GetValue().Guid);
        CHECK_FALSE(fileCheck.GetValue().IsGenerated());
        CHECK_FALSE(fileCheck.GetValue().ResolvedPath.empty());

        Project::ProjectManager::GetInstance().CloseProject();
        std::filesystem::remove_all(projectRoot, errorCode);
    }

    TEST_CASE("Generated asset runtime provider loads and reloads input actions")
    {
        using namespace Limitless;

        const std::filesystem::path projectRoot = MakeTempProjectRoot("GeneratedAssetProviderInputActions");
        std::error_code errorCode;
        Assets::AssetManager::ClearCaches();
        Project::ProjectManager::GetInstance().CloseProject();
        std::filesystem::remove_all(projectRoot, errorCode);
        errorCode.clear();

        const auto createProjectResult = Project::ProjectManager::GetInstance().CreateProjectRoot(projectRoot, "GeneratedAssetProviderInputActions");
        REQUIRE(createProjectResult.IsSuccess());

        const std::string guid = "generated-provider-guid-008";
        const std::string key = "Generated/Input/RuntimeActions";
        REQUIRE(Assets::AssetDatabase::GetInstance().RegisterGeneratedAsset(guid, key, Assets::AssetType::InputActions).IsSuccess());

        auto payload = std::make_shared<std::string>(
            R"({"maps":[{"name":"Gameplay","actions":[{"name":"Jump","type":"Button","bindings":[{"binding":"KeyboardButton","scancode":4}]}]}]})");

        Assets::GeneratedAssetRuntimeRegistry::Entry entry;
        entry.LoadText = [payload]() -> Result<std::string> { return *payload; };
        Assets::GeneratedAssetRuntimeRegistry::GetInstance().Register(key, std::move(entry));

        auto asset = Assets::InputActionsAssetResource::LoadBlocking(key);
        REQUIRE(asset != nullptr);
        REQUIRE(asset->GetValue() != nullptr);
        REQUIRE(asset->GetValue()->FindMap("Gameplay") != nullptr);
        REQUIRE(asset->GetValue()->FindMap("Gameplay")->FindAction("Jump") != nullptr);

        const auto stableValue = asset->GetValue();
        const uint64_t revisionBefore = asset->GetRevision();
        *payload = R"({"maps":[{"name":"Gameplay","actions":[{"name":"Dash","type":"Button","bindings":[{"binding":"KeyboardButton","scancode":7}]}]}]})";

        REQUIRE(asset->Reload());
        CHECK(asset->GetRevision() > revisionBefore);
        CHECK(asset->GetValue() == stableValue);
        REQUIRE(asset->GetValue()->FindMap("Gameplay") != nullptr);
        CHECK(asset->GetValue()->FindMap("Gameplay")->FindAction("Jump") == nullptr);
        CHECK(asset->GetValue()->FindMap("Gameplay")->FindAction("Dash") != nullptr);

        Assets::GeneratedAssetRuntimeRegistry::GetInstance().Unregister(key);
        Assets::AssetManager::ClearCaches();
        Project::ProjectManager::GetInstance().CloseProject();
        std::filesystem::remove_all(projectRoot, errorCode);
    }

    TEST_CASE("Generated dependents reload during import cascade")
    {
        using namespace Limitless;

        const std::filesystem::path projectRoot = MakeTempProjectRoot("GeneratedAssetCascadeReload");
        std::error_code errorCode;
        Assets::AssetManager::ClearCaches();
        Project::ProjectManager::GetInstance().CloseProject();
        std::filesystem::remove_all(projectRoot, errorCode);
        errorCode.clear();

        const auto createProjectResult = Project::ProjectManager::GetInstance().CreateProjectRoot(projectRoot, "GeneratedAssetCascadeReload");
        REQUIRE(createProjectResult.IsSuccess());

        const std::filesystem::path shaderPath = projectRoot / "Assets" / "Shaders" / "Source.glsl";
        std::filesystem::create_directories(shaderPath.parent_path(), errorCode);
        REQUIRE_FALSE(errorCode);
        {
            std::ofstream out(shaderPath, std::ios::out | std::ios::binary | std::ios::trunc);
            REQUIRE(out.is_open());
            out << "#version 330 core\nvoid main() {}\n";
        }

        const std::string sourceKey = "Assets/Shaders/Source.glsl";
        const auto sourceImport = Assets::AssetDatabase::GetInstance().ImportOrUpdate(sourceKey, Assets::AssetType::Shader);
        REQUIRE(sourceImport.IsSuccess());

        const std::string generatedGuid = "generated-cascade-guid-009";
        const std::string generatedKey = "Generated/Atlases/CascadeAtlas";
        REQUIRE(Assets::AssetDatabase::GetInstance().RegisterGeneratedAsset(generatedGuid, generatedKey, Assets::AssetType::Texture2D).IsSuccess());
        REQUIRE(Assets::AssetDatabase::GetInstance().SetDependencies(generatedGuid, { sourceImport.GetValue().Guid }).IsSuccess());

        auto reloadCount = std::make_shared<int>(0);
        Assets::GeneratedAssetRuntimeRegistry::Entry entry;
        entry.Reload = [reloadCount]() -> bool {
            ++(*reloadCount);
            return true;
        };
        Assets::GeneratedAssetRuntimeRegistry::GetInstance().Register(generatedKey, std::move(entry));

        {
            std::ofstream out(shaderPath, std::ios::out | std::ios::binary | std::ios::trunc);
            REQUIRE(out.is_open());
            out << "#version 330 core\nvoid main() { }\n";
        }
        std::filesystem::last_write_time(shaderPath, std::filesystem::last_write_time(shaderPath) + std::chrono::seconds(2), errorCode);
        REQUIRE_FALSE(errorCode);

        const auto reimportResult = Assets::AssetImportPipeline::ReimportChanged(true);
        REQUIRE(reimportResult.IsSuccess());
        CHECK(*reloadCount == 1);
        CHECK(std::find(reimportResult.GetValue().ImportedKeys.begin(),
                        reimportResult.GetValue().ImportedKeys.end(),
                        generatedKey) != reimportResult.GetValue().ImportedKeys.end());

        Assets::GeneratedAssetRuntimeRegistry::GetInstance().Unregister(generatedKey);
        Assets::AssetManager::ClearCaches();
        Project::ProjectManager::GetInstance().CloseProject();
        std::filesystem::remove_all(projectRoot, errorCode);
    }

    TEST_CASE("Asset import pipeline and database expose telemetry")
    {
        using namespace Limitless;

        const std::filesystem::path projectRoot = MakeTempProjectRoot("AssetTelemetry");
        std::error_code errorCode;
        Assets::AssetManager::ClearCaches();
        Project::ProjectManager::GetInstance().CloseProject();
        std::filesystem::remove_all(projectRoot, errorCode);
        errorCode.clear();

        const auto createProjectResult = Project::ProjectManager::GetInstance().CreateProjectRoot(projectRoot, "AssetTelemetry");
        REQUIRE(createProjectResult.IsSuccess());

        const std::filesystem::path shaderPath = projectRoot / "Assets" / "Shaders" / "Telemetry.glsl";
        std::filesystem::create_directories(shaderPath.parent_path(), errorCode);
        REQUIRE_FALSE(errorCode);
        {
            std::ofstream out(shaderPath, std::ios::out | std::ios::binary | std::ios::trunc);
            REQUIRE(out.is_open());
            out << "#version 330 core\nvoid main() {}\n";
        }

        const auto importResult = Assets::AssetImportPipeline::ReimportAll(false);
        REQUIRE(importResult.IsSuccess());
        CHECK(importResult.GetValue().DiscoveredFiles >= 1);
        CHECK(importResult.GetValue().WorkerCount >= 1);
        CHECK(importResult.GetValue().DiscoveryMs >= 0.0);
        CHECK(importResult.GetValue().TotalMs >= importResult.GetValue().DiscoveryMs);

        const auto beforeTelemetry = Assets::AssetDatabase::GetInstance().GetCacheTelemetry();
        Assets::AssetDatabase::GetInstance().Reset();
        const auto reloadRecord = Assets::AssetDatabase::GetInstance().FindByKey("Assets/Shaders/Telemetry.glsl");
        REQUIRE(reloadRecord.IsSuccess());
        const auto afterTelemetry = Assets::AssetDatabase::GetInstance().GetCacheTelemetry();
        CHECK(afterTelemetry.CacheHits + afterTelemetry.CacheMisses >
              beforeTelemetry.CacheHits + beforeTelemetry.CacheMisses);

        Project::ProjectManager::GetInstance().CloseProject();
        std::filesystem::remove_all(projectRoot, errorCode);
    }
}
