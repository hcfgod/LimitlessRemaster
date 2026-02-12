#include "ProjectAssetOperations.h"

#include "Assets/AssetPaths.h"
#include "Core/Debug/Log.h"

namespace Limitless::ProjectAssetOperations
{
    bool CreateFolderInDirectory(const std::filesystem::path& assetsDirectory,
                                 const std::filesystem::path& parentRelativePath,
                                 const std::string& folderName)
    {
        if (folderName.empty())
            return false;

        const std::filesystem::path parentDirectory = assetsDirectory / parentRelativePath;
        std::error_code errorCode;
        if (!std::filesystem::exists(parentDirectory, errorCode) || !std::filesystem::is_directory(parentDirectory, errorCode))
            return false;

        const std::filesystem::path candidateDirectory = parentDirectory / folderName;
        if (std::filesystem::exists(candidateDirectory, errorCode))
        {
            LT_CORE_WARN("CreateFolderInDirectory: folder already exists: {}", candidateDirectory.string());
            return false;
        }

        return std::filesystem::create_directory(candidateDirectory, errorCode);
    }

    bool DeleteFolderInAssets(const std::filesystem::path& assetsDirectory,
                              const std::filesystem::path& folderRelativePath)
    {
        const std::filesystem::path folderPath = assetsDirectory / folderRelativePath;
        std::error_code errorCode;
        if (!std::filesystem::exists(folderPath, errorCode) || !std::filesystem::is_directory(folderPath, errorCode))
            return false;

        std::filesystem::remove_all(folderPath, errorCode);
        return !errorCode;
    }

    bool RenameFolderInAssets(const std::filesystem::path& assetsDirectory,
                              const std::filesystem::path& folderRelativePath,
                              const std::string& newName)
    {
        if (newName.empty())
            return false;

        const std::filesystem::path parentDirectory = assetsDirectory / folderRelativePath.parent_path();
        const std::filesystem::path oldPath = assetsDirectory / folderRelativePath;
        const std::filesystem::path newPath = parentDirectory / newName;

        std::error_code errorCode;
        if (!std::filesystem::exists(oldPath, errorCode) || !std::filesystem::is_directory(oldPath, errorCode))
            return false;

        if (std::filesystem::exists(newPath, errorCode))
        {
            LT_CORE_WARN("RenameFolderInAssets: destination already exists: {}", newPath.string());
            return false;
        }

        std::filesystem::rename(oldPath, newPath, errorCode);
        return !errorCode;
    }

    bool MoveFolderToFolder(const std::string& folderAssetKey,
                            const std::filesystem::path& destinationFolderRelativePath)
    {
        auto resolveResult = Assets::ResolveAssetKeyToPath(folderAssetKey);
        if (resolveResult.IsFailure())
            return false;

        const std::filesystem::path sourcePath = resolveResult.GetValue();
        if (!std::filesystem::is_directory(sourcePath))
            return false;

        auto rootResult = Assets::FindProjectRootFromWorkingDirectory();
        if (rootResult.IsFailure())
            return false;

        const std::filesystem::path destinationDirectory = rootResult.GetValue() / "Assets" / destinationFolderRelativePath;
        std::error_code errorCode;
        if (!std::filesystem::exists(destinationDirectory, errorCode) || !std::filesystem::is_directory(destinationDirectory, errorCode))
            return false;

        const std::filesystem::path folderName = sourcePath.filename();
        const std::filesystem::path destinationPath = destinationDirectory / folderName;

        if (sourcePath == destinationPath)
            return true;

        // Prevent moving a folder into itself or one of its descendants.
        auto relativePath = std::filesystem::relative(destinationDirectory, sourcePath, errorCode);
        if (!errorCode)
        {
            const std::string relativePathString = relativePath.generic_string();
            if (relativePathString.find("..") != 0 && relativePathString != ".")
            {
                LT_CORE_WARN("MoveFolderToFolder: cannot move folder into itself or descendant");
                return false;
            }
        }

        if (std::filesystem::exists(destinationPath, errorCode))
        {
            LT_CORE_WARN("MoveFolderToFolder: destination already exists: {}", destinationPath.string());
            return false;
        }

        std::filesystem::rename(sourcePath, destinationPath, errorCode);
        return !errorCode;
    }

    bool MoveAssetToFolder(const std::string& assetKey,
                           const std::filesystem::path& destinationFolderRelativePath)
    {
        auto resolveResult = Assets::ResolveAssetKeyToPath(assetKey);
        if (resolveResult.IsFailure())
            return false;

        const std::filesystem::path sourcePath = resolveResult.GetValue();
        if (!std::filesystem::is_regular_file(sourcePath))
            return false;

        auto rootResult = Assets::FindProjectRootFromWorkingDirectory();
        if (rootResult.IsFailure())
            return false;

        const std::filesystem::path destinationDirectory = rootResult.GetValue() / "Assets" / destinationFolderRelativePath;
        std::error_code errorCode;
        if (!std::filesystem::exists(destinationDirectory, errorCode) || !std::filesystem::is_directory(destinationDirectory, errorCode))
            return false;

        const std::filesystem::path filename = sourcePath.filename();
        const std::filesystem::path destinationPath = destinationDirectory / filename;

        if (sourcePath == destinationPath)
            return true;

        if (std::filesystem::exists(destinationPath, errorCode))
        {
            LT_CORE_WARN("MoveAssetToFolder: destination already exists: {}", destinationPath.string());
            return false;
        }

        std::filesystem::rename(sourcePath, destinationPath, errorCode);
        if (errorCode)
            return false;

        // Move companion .meta file if present.
        const std::filesystem::path metaPath = sourcePath.parent_path() / (sourcePath.filename().string() + ".meta");
        if (std::filesystem::exists(metaPath, errorCode))
        {
            const std::filesystem::path destinationMetaPath = destinationDirectory / (filename.string() + ".meta");
            std::filesystem::rename(metaPath, destinationMetaPath, errorCode);
        }

        return true;
    }
}
