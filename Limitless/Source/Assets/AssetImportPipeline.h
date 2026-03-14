#pragma once

#include "Assets/AssetDatabase.h"
#include "Core/Error.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Limitless::Assets
{
    struct AssetImportStatistics final
    {
        size_t DiscoveredFiles = 0;
        size_t Imported = 0;
        size_t SkippedUpToDate = 0;
        size_t MissingOnDisk = 0;
        size_t Errors = 0;

        // Keys imported or reimported (includes dependents triggered by cascade).
        std::vector<std::string> ImportedKeys;
    };

    struct AssetDatabaseValidationIssue final
    {
        enum class Type
        {
            MissingFileForRecord,
            StaleKeyMapping,
            DuplicateGuidForDifferentKeys,
            SelfDependency,
            MissingDependencyRecord,
            DependencyCycle
        };

        Type IssueType = Type::MissingFileForRecord;
        std::string Message;
        std::string Key;
        std::string Guid;
        std::string ResolvedPath;
    };

    /// Editor/tooling import pipeline for project assets.
    ///
    /// This pipeline is distinct from runtime `AssetImporter<T>::LoadAsync`:
    /// - It scans `Assets/` for known types
    /// - It performs incremental import (only changed assets)
    /// - It can cascade to dependents
    namespace AssetImportPipeline
    {
        Result<AssetImportStatistics> ReimportAll(bool includeDependents = true);
        Result<AssetImportStatistics> ReimportChanged(bool includeDependents = true);

        Result<std::vector<AssetDatabaseValidationIssue>> ValidateAssetDatabase();
    }
}

