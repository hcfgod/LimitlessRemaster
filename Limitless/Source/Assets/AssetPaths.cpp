#include "Assets/AssetPaths.h"

#include "Core/Debug/Log.h"

#include <optional>
#include <mutex>
#include <cstdlib>

namespace Limitless::Assets
{
    static std::optional<std::filesystem::path> s_AssetRootOverride;

    // Cache `FindProjectRootFromWorkingDirectory()` because it is called frequently during asset loads.
    // IMPORTANT: this cache must be invalidated when callers set an explicit override.
    static std::mutex s_CacheMutex;
    static bool s_HasCached = false;
    static Result<std::filesystem::path> s_CachedResult(ErrorCode::InvalidState, "Project root not cached");

    static std::optional<std::filesystem::path> TryFindProjectRootByMarker(const std::filesystem::path& startingDirectory)
    {
        std::error_code ec;
        std::filesystem::path probe = startingDirectory;

        for (int depth = 0; depth < 32; ++depth)
        {
            const std::filesystem::path marker = probe / "Project" / "Project.json";
            if (std::filesystem::exists(marker, ec) && std::filesystem::is_regular_file(marker, ec))
            {
                return probe;
            }

            if (!probe.has_parent_path())
            {
                break;
            }

            const std::filesystem::path parent = probe.parent_path();
            if (parent == probe)
            {
                break;
            }

            probe = parent;
        }

        return std::nullopt;
    }

    void SetAssetRootDirectory(const std::filesystem::path& rootDirectory)
    {
        if (rootDirectory.empty())
        {
            s_AssetRootOverride.reset();
            std::lock_guard<std::mutex> lock(s_CacheMutex);
            s_HasCached = false;
            return;
        }

        s_AssetRootOverride = std::filesystem::weakly_canonical(rootDirectory);
        std::lock_guard<std::mutex> lock(s_CacheMutex);
        s_HasCached = false;
    }

    Result<std::filesystem::path> FindProjectRootFromWorkingDirectory()
    {
        {
            std::lock_guard<std::mutex> lock(s_CacheMutex);
            if (s_HasCached)
            {
                return s_CachedResult;
            }
        }

        // Explicit override (preferred).
        if (s_AssetRootOverride.has_value())
        {
            Result<std::filesystem::path> ok = s_AssetRootOverride.value();
            std::lock_guard<std::mutex> lock(s_CacheMutex);
            s_CachedResult = ok;
            s_HasCached = true;
            return ok;
        }

        // Environment override (optional):
        // Set `LIMITLESS_ASSET_ROOT` to a directory that contains `Assets/`.
        if (const char* env = std::getenv("LIMITLESS_ASSET_ROOT"))
        {
            if (env[0] != '\0')
            {
                std::filesystem::path candidate = std::filesystem::weakly_canonical(std::filesystem::path(env));
                if (candidate.filename() == "Assets")
                {
                    candidate = candidate.parent_path();
                }

                std::error_code envEc;
                if (std::filesystem::exists(candidate, envEc) && std::filesystem::is_directory(candidate, envEc))
                {
                    const std::filesystem::path assetsDir = candidate / "Assets";
                    if (std::filesystem::exists(assetsDir, envEc) && std::filesystem::is_directory(assetsDir, envEc))
                    {
                        Result<std::filesystem::path> ok = candidate;
                        std::lock_guard<std::mutex> lock(s_CacheMutex);
                        s_CachedResult = ok;
                        s_HasCached = true;
                        return ok;
                    }
                }
            }
        }

        std::error_code ec;
        std::filesystem::path current = std::filesystem::current_path(ec);
        if (ec)
        {
            Result<std::filesystem::path> fail(ErrorCode::FileAccessDenied, "Failed to query current working directory");
            std::lock_guard<std::mutex> lock(s_CacheMutex);
            s_CachedResult = fail;
            s_HasCached = true;
            return fail;
        }

        // Preferred (Unity-grade): a project marker file makes project discovery deterministic.
        if (auto markerRoot = TryFindProjectRootByMarker(current); markerRoot.has_value())
        {
            Result<std::filesystem::path> ok = markerRoot.value();
            std::lock_guard<std::mutex> lock(s_CacheMutex);
            s_CachedResult = ok;
            s_HasCached = true;
            return ok;
        }

        // Walk upward until we find a directory containing `Assets/`.
        //
        // IMPORTANT:
        // The working directory may be `Sandbox/`, and that directory can also contain an `Assets/` folder.
        // If you have multiple `Assets/` trees on disk, prefer using SetAssetRootDirectory() or LIMITLESS_ASSET_ROOT.
        std::filesystem::path probeAssets = current;
        for (int depth = 0; depth < 32; ++depth)
        {
            const std::filesystem::path assetsDir = probeAssets / "Assets";
            if (std::filesystem::exists(assetsDir, ec) && std::filesystem::is_directory(assetsDir, ec))
            {
                Result<std::filesystem::path> ok = probeAssets;
                std::lock_guard<std::mutex> lock(s_CacheMutex);
                s_CachedResult = ok;
                s_HasCached = true;
                return ok;
            }

            if (!probeAssets.has_parent_path())
            {
                break;
            }

            const std::filesystem::path parent = probeAssets.parent_path();
            if (parent == probeAssets)
            {
                break;
            }

            probeAssets = parent;
        }

        Result<std::filesystem::path> fail(
            ErrorCode::ResourceNotFound,
            "Could not locate project root (no 'Project/Project.json' marker and no 'Assets/' directory found)");
        std::lock_guard<std::mutex> lock(s_CacheMutex);
        s_CachedResult = fail;
        s_HasCached = true;
        return fail;
    }

    Result<std::filesystem::path> ResolveAssetKeyToPath(const std::string& assetKey)
    {
        if (assetKey.empty())
        {
            return Result<std::filesystem::path>(ErrorCode::InvalidArgument, "Asset key is empty");
        }

        std::filesystem::path keyPath(assetKey);
        if (keyPath.is_absolute())
        {
            return std::filesystem::weakly_canonical(keyPath);
        }

        // Unity-style keys.
        if (assetKey.rfind("Assets/", 0) == 0 || assetKey.rfind("Assets\\", 0) == 0)
        {
            auto rootResult = FindProjectRootFromWorkingDirectory();
            if (rootResult.IsFailure())
            {
                return Result<std::filesystem::path>(rootResult.GetError());
            }

            const std::filesystem::path root = rootResult.GetValue();
            const std::filesystem::path resolved = root / keyPath;
            return std::filesystem::weakly_canonical(resolved);
        }

        // Otherwise: relative to CWD.
        std::error_code ec;
        const std::filesystem::path cwd = std::filesystem::current_path(ec);
        if (ec)
        {
            return Result<std::filesystem::path>(ErrorCode::FileAccessDenied, "Failed to query current working directory");
        }

        return std::filesystem::weakly_canonical(cwd / keyPath);
    }
}

