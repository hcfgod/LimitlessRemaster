#include "Assets/AssetImportPipeline.h"

#include "Assets/AssetManager.h"
#include "Assets/AssetPaths.h"
#include "Assets/AssetTypes.h"

#include "Core/Debug/Log.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <deque>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace Limitless::Assets::AssetImportPipeline
{
    namespace
    {
        bool EndsWith(const std::string& s, const std::string& suffix)
        {
            if (s.size() < suffix.size()) return false;
            return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
        }

        std::optional<AssetType> GuessTypeFromPath(const std::filesystem::path& path)
        {
            const std::string name = path.filename().string();
            const std::string ext = path.extension().string();

            if (EndsWith(name, ".scene.json")) return AssetType::Scene;
            if (EndsWith(name, ".prefab.json")) return AssetType::Prefab;
            if (EndsWith(name, ".tilemap.json")) return AssetType::Tilemap;
            if (EndsWith(name, ".tileset.json")) return AssetType::Tileset;
            if (EndsWith(name, ".animationclip.json") || EndsWith(name, ".animation.json") || EndsWith(name, ".anim.json"))
                return AssetType::AnimationClip;
            if (EndsWith(name, ".animcontroller.json") || EndsWith(name, ".animatorcontroller.json"))
                return AssetType::AnimatorController;
            if (EndsWith(name, ".material.json")) return AssetType::Material;
            if (EndsWith(name, ".inputactions.json")) return AssetType::InputActions;
            if (EndsWith(name, ".audiomixer.json")) return AssetType::AudioMixer;
            if (ext == ".glsl") return AssetType::Shader;

            // Textures
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" ||
                ext == ".hdr" || ext == ".psd" || ext == ".gif" || ext == ".ppm" || ext == ".pnm")
            {
                return AssetType::Texture2D;
            }

            // Audio
            if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac")
            {
                return AssetType::AudioClip;
            }

            return std::nullopt;
        }

        int64_t GetLastWriteTimeTicksOrZero(const std::filesystem::path& path)
        {
            std::error_code ec;
            const auto t = std::filesystem::last_write_time(path, ec);
            if (ec) return 0;
            return static_cast<int64_t>(t.time_since_epoch().count());
        }

        uint64_t GetFileSizeOrZero(const std::filesystem::path& path)
        {
            std::error_code ec;
            const uint64_t size = std::filesystem::file_size(path, ec);
            return ec ? 0ull : size;
        }

        bool IsUpToDate(const AssetDatabase::Record& record, const std::filesystem::path& filePath)
        {
            // Best-effort incremental import based on size + last-write ticks + settings hash + importer version.
            const uint64_t sizeBytes = GetFileSizeOrZero(filePath);
            const int64_t lastWrite = GetLastWriteTimeTicksOrZero(filePath);
            if (sizeBytes == 0 || lastWrite == 0)
            {
                // If we can't stat the file reliably, force import.
                return false;
            }

            return record.SourceSizeBytes == sizeBytes &&
                   record.SourceLastWriteTimeTicks == lastWrite &&
                   record.ImporterVersion == 1;
        }

        void ReloadIfCached(const std::string& key)
        {
            if (const std::shared_ptr<Asset> cached = AssetManager::GetCachedByKey(key))
            {
                (void)cached->Reload();
            }
        }

        struct ImportJob
        {
            std::string Key;
            AssetType Type = AssetType::Unknown;
        };

        Result<std::vector<ImportJob>> DiscoverKnownAssets(const std::filesystem::path& projectRoot)
        {
            std::vector<ImportJob> jobs;

            const std::filesystem::path assetsRoot = projectRoot / "Assets";
            if (!std::filesystem::exists(assetsRoot))
            {
                return Result<std::vector<ImportJob>>(ErrorCode::ResourceNotFound, "Assets/ directory not found: " + assetsRoot.string());
            }

            std::error_code ec;
            for (auto it = std::filesystem::recursive_directory_iterator(assetsRoot, ec);
                 it != std::filesystem::recursive_directory_iterator();
                 it.increment(ec))
            {
                if (ec)
                {
                    ec.clear();
                    continue;
                }

                if (!it->is_regular_file(ec))
                {
                    ec.clear();
                    continue;
                }

                const auto& filePath = it->path();
                if (filePath.extension() == ".meta")
                {
                    continue;
                }

                auto typeOpt = GuessTypeFromPath(filePath);
                if (!typeOpt.has_value())
                {
                    continue;
                }

                std::filesystem::path rel = std::filesystem::relative(filePath, projectRoot, ec);
                if (ec)
                {
                    ec.clear();
                    continue;
                }

                ImportJob job;
                job.Key = rel.generic_string();
                job.Type = *typeOpt;
                jobs.push_back(std::move(job));
            }

            return jobs;
        }

        Result<AssetImportStatistics> ImportJobs(const std::filesystem::path& projectRoot, const std::vector<ImportJob>& jobs, bool changedOnly, bool includeDependents)
        {
            AssetImportStatistics stats;
            stats.DiscoveredFiles = jobs.size();

            std::vector<std::string> changedGuids;
            changedGuids.reserve(64);

            for (const auto& job : jobs)
            {
                if (job.Key.empty())
                {
                    continue;
                }

                const std::filesystem::path abs = projectRoot / job.Key;
                if (!std::filesystem::exists(abs))
                {
                    stats.MissingOnDisk++;
                    continue;
                }

                nlohmann::json settings = nlohmann::json::object();
                auto existingRecord = AssetDatabase::GetInstance().FindByKey(job.Key);
                if (existingRecord.IsSuccess())
                {
                    settings = existingRecord.GetValue().ImporterSettings;
                }

                if (changedOnly && existingRecord.IsSuccess() && IsUpToDate(existingRecord.GetValue(), abs))
                {
                    stats.SkippedUpToDate++;
                    continue;
                }

                const auto importResult = AssetDatabase::GetInstance().ImportOrUpdate(job.Key, job.Type, settings);
                if (importResult.IsFailure())
                {
                    stats.Errors++;
                    LT_CORE_WARN("AssetImportPipeline: import failed key='{}': {}", job.Key, importResult.GetError().GetErrorMessage());
                    continue;
                }

                stats.Imported++;
                stats.ImportedKeys.push_back(job.Key);
                changedGuids.push_back(importResult.GetValue().Guid);

                ReloadIfCached(job.Key);
            }

            if (!includeDependents || changedGuids.empty())
            {
                return stats;
            }

            // Cascade to dependents via reverse dependency graph.
            std::deque<std::string> queue;
            std::unordered_set<std::string> visited;
            visited.reserve(512);

            for (const auto& guid : changedGuids)
            {
                if (!guid.empty())
                {
                    queue.push_back(guid);
                }
            }

            while (!queue.empty())
            {
                const std::string currentGuid = queue.front();
                queue.pop_front();

                if (!visited.emplace(currentGuid).second)
                {
                    continue;
                }

                const auto dependents = AssetDatabase::GetInstance().GetDependentsOf(currentGuid);
                for (const auto& dep : dependents)
                {
                    // Re-import dependent metadata (ensures GUID + refreshed fingerprints).
                    const auto reimport = AssetDatabase::GetInstance().ImportOrUpdate(dep.Key, dep.Type, dep.ImporterSettings);
                    if (reimport.IsSuccess())
                    {
                        stats.ImportedKeys.push_back(dep.Key);
                        ReloadIfCached(dep.Key);
                        queue.push_back(dep.Guid);
                    }
                }
            }

            return stats;
        }
    }

    Result<AssetImportStatistics> ReimportAll(bool includeDependents)
    {
        const auto rootResult = FindProjectRootFromWorkingDirectory();
        if (rootResult.IsFailure())
        {
            return Result<AssetImportStatistics>(rootResult.GetError());
        }

        const std::filesystem::path projectRoot = rootResult.GetValue();
        const auto jobsResult = DiscoverKnownAssets(projectRoot);
        if (jobsResult.IsFailure())
        {
            return Result<AssetImportStatistics>(jobsResult.GetError());
        }

        return ImportJobs(projectRoot, jobsResult.GetValue(), /*changedOnly=*/false, includeDependents);
    }

    Result<AssetImportStatistics> ReimportChanged(bool includeDependents)
    {
        const auto rootResult = FindProjectRootFromWorkingDirectory();
        if (rootResult.IsFailure())
        {
            return Result<AssetImportStatistics>(rootResult.GetError());
        }

        const std::filesystem::path projectRoot = rootResult.GetValue();
        const auto jobsResult = DiscoverKnownAssets(projectRoot);
        if (jobsResult.IsFailure())
        {
            return Result<AssetImportStatistics>(jobsResult.GetError());
        }

        return ImportJobs(projectRoot, jobsResult.GetValue(), /*changedOnly=*/true, includeDependents);
    }

    Result<std::vector<AssetDatabaseValidationIssue>> ValidateAssetDatabase()
    {
        const auto rootResult = FindProjectRootFromWorkingDirectory();
        if (rootResult.IsFailure())
        {
            return Result<std::vector<AssetDatabaseValidationIssue>>(rootResult.GetError());
        }

        const std::filesystem::path projectRoot = rootResult.GetValue();

        std::vector<AssetDatabaseValidationIssue> issues;
        const auto records = AssetDatabase::GetInstance().GetAllRecords();

        std::unordered_map<std::string, std::string> firstKeyByGuid;
        firstKeyByGuid.reserve(records.size());

        for (const auto& r : records)
        {
            if (r.Guid.empty() || r.Key.empty())
            {
                continue;
            }

            // Missing file on disk.
            std::error_code ec;
            if (!r.ResolvedPath.empty() && !std::filesystem::exists(std::filesystem::path(r.ResolvedPath), ec))
            {
                AssetDatabaseValidationIssue issue;
                issue.IssueType = AssetDatabaseValidationIssue::Type::MissingFileForRecord;
                issue.Guid = r.Guid;
                issue.Key = r.Key;
                issue.ResolvedPath = r.ResolvedPath;
                issue.Message = "Missing file on disk for record";
                issues.push_back(std::move(issue));
            }

            // Duplicate GUID (should never happen, but detect and report).
            if (auto it = firstKeyByGuid.find(r.Guid); it != firstKeyByGuid.end())
            {
                if (it->second != r.Key)
                {
                    AssetDatabaseValidationIssue issue;
                    issue.IssueType = AssetDatabaseValidationIssue::Type::DuplicateGuidForDifferentKeys;
                    issue.Guid = r.Guid;
                    issue.Key = r.Key;
                    issue.Message = "Duplicate GUID mapped to multiple keys ('" + it->second + "' and '" + r.Key + "')";
                    issues.push_back(std::move(issue));
                }
            }
            else
            {
                firstKeyByGuid.emplace(r.Guid, r.Key);
            }
        }

        return issues;
    }
}

