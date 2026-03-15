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

        // Telemetry: wall-time in milliseconds for each import stage.
        double DiscoveryMs = 0.0;
        double PrepMs = 0.0;
        double CommitMs = 0.0;
        double CascadeMs = 0.0;
        double TotalMs = 0.0;
        size_t WorkerCount = 0;
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

    /// Configuration for parallel import pipeline.
    struct ParallelImportConfig final
    {
        // When true, all import prep work runs on the calling thread (useful for
        // debugging and easier bisecting of import issues).
        bool ForceSequential = false;

        // Grain size for ParallelFor chunking.  0 = auto (JobSystem picks a
        // reasonable chunk size based on worker count).
        size_t GrainSize = 0;
    };

    /// Editor/tooling import pipeline for project assets.
    ///
    /// This pipeline is distinct from runtime `AssetImporter<T>::LoadAsync`:
    /// - It scans `Assets/` for known types
    /// - It performs incremental import (only changed assets)
    /// - It can cascade to dependents
    ///
    /// Import is split into stages:
    /// 1. Discovery – walk Assets/ and build a job list.
    /// 2. Parallel prep – resolve paths, stat files, load/create GUIDs
    ///    (uses the engine JobSystem when available).
    /// 3. Sorted commit – deterministic batch upsert into AssetDatabase
    ///    with a single save-to-disk.
    /// 4. Reload – notify in-memory asset caches.
    /// 5. Cascade – BFS through reverse dependency graph.
    namespace AssetImportPipeline
    {
        Result<AssetImportStatistics> ReimportAll(bool includeDependents = true,
                                                  const ParallelImportConfig& config = {});
        Result<AssetImportStatistics> ReimportChanged(bool includeDependents = true,
                                                      const ParallelImportConfig& config = {});

        Result<std::vector<AssetDatabaseValidationIssue>> ValidateAssetDatabase();
    }
}

