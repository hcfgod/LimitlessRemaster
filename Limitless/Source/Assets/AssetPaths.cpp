#include "Assets/AssetPaths.h"

#include "Core/Debug/Log.h"

#include <optional>

namespace Limitless::Assets
{
    Result<std::filesystem::path> FindProjectRootFromWorkingDirectory()
    {
        std::error_code ec;
        std::filesystem::path current = std::filesystem::current_path(ec);
        if (ec)
        {
            return Result<std::filesystem::path>(ErrorCode::FileAccessDenied, "Failed to query current working directory");
        }

        // Walk upward until we find a directory containing `Assets/`.
        //
        // IMPORTANT:
        // The working directory may be `Sandbox/`, and that directory can also contain an `Assets/` folder
        // for sandbox-specific content. For Unity-style project assets we want the *repository root*.
        //
        // We prefer a directory that contains both:
        // - `Assets/` directory
        // - `LimitlessRemaster.sln` (repo root marker)
        //
        // If that marker isn't found, we fall back to the first directory containing `Assets/`.
        std::filesystem::path probe = current;
        std::optional<std::filesystem::path> firstAssetsCandidate;
        for (int depth = 0; depth < 32; ++depth)
        {
            const std::filesystem::path assetsDir = probe / "Assets";
            if (std::filesystem::exists(assetsDir, ec) && std::filesystem::is_directory(assetsDir, ec))
            {
                if (!firstAssetsCandidate.has_value())
                {
                    firstAssetsCandidate = probe;
                }

                const std::filesystem::path solutionPath = probe / "LimitlessRemaster.sln";
                if (std::filesystem::exists(solutionPath, ec))
                {
                    return probe;
                }
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

        if (firstAssetsCandidate.has_value())
        {
            LT_CORE_WARN("Assets: falling back to first 'Assets' directory candidate (no solution marker found). Root='{}' CWD='{}'",
                         firstAssetsCandidate->string(), current.string());
            return firstAssetsCandidate.value();
        }

        LT_CORE_ERROR("Assets: could not find project root containing an 'Assets' directory. CWD='{}'",
                      current.string());
        return Result<std::filesystem::path>(ErrorCode::ResourceNotFound, "Could not locate project root containing an 'Assets' directory");
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

