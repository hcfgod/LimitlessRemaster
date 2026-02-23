#include "ProjectAssetOperations.h"

#include "Assets/AssetDatabase.h"
#include "Assets/AssetImportPipeline.h"
#include "Assets/AssetPaths.h"
#include "Core/Debug/Log.h"

#include <algorithm>
#include <cctype>

namespace Limitless::ProjectAssetOperations
{
    namespace
    {
        constexpr const char* kSceneSuffix = ".scene.json";
        constexpr const char* kMaterialSuffix = ".material.json";
        constexpr const char* kInputActionsSuffix = ".inputactions.json";

        bool EndsWithCaseInsensitive(const std::string& value, const std::string& suffix)
        {
            if (value.size() < suffix.size())
                return false;

            const size_t offset = value.size() - suffix.size();
            for (size_t index = 0; index < suffix.size(); ++index)
            {
                const unsigned char left = static_cast<unsigned char>(value[offset + index]);
                const unsigned char right = static_cast<unsigned char>(suffix[index]);
                if (std::tolower(left) != std::tolower(right))
                    return false;
            }
            return true;
        }

        std::string InferSuffixForAssetFileName(const std::string& fileName)
        {
            if (EndsWithCaseInsensitive(fileName, kSceneSuffix))
                return kSceneSuffix;
            if (EndsWithCaseInsensitive(fileName, kMaterialSuffix))
                return kMaterialSuffix;
            if (EndsWithCaseInsensitive(fileName, kInputActionsSuffix))
                return kInputActionsSuffix;

            const std::filesystem::path path(fileName);
            return path.has_extension() ? path.extension().string() : std::string{};
        }

        std::string SanitizeFileNameBase(std::string value)
        {
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
                value.erase(value.begin());
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
                value.pop_back();

            for (char& character : value)
            {
                if (character == '/' || character == '\\' || character == ':' || character == '*' ||
                    character == '?' || character == '"' || character == '<' || character == '>' || character == '|')
                    character = '_';
            }
            return value;
        }

        std::filesystem::path GetUniquePathInDirectory(const std::filesystem::path& destinationDirectory, const std::filesystem::path& sourceName)
        {
            std::filesystem::path candidate = destinationDirectory / sourceName;
            std::error_code ec;
            if (!std::filesystem::exists(candidate, ec))
            {
                return candidate;
            }

            const std::string stem = sourceName.stem().string();
            const std::string ext = sourceName.extension().string();
            for (int index = 1; index <= 4096; ++index)
            {
                const std::filesystem::path numbered = destinationDirectory / (stem + " (" + std::to_string(index) + ")" + ext);
                if (!std::filesystem::exists(numbered, ec))
                {
                    return numbered;
                }
            }

            return candidate;
        }
    }

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

    bool RenameAssetInAssets(const std::filesystem::path& assetsDirectory,
                             const std::filesystem::path& assetRelativePath,
                             const std::string& newDisplayName,
                             std::filesystem::path* outNewAssetRelativePath)
    {
        std::string sanitizedBaseName = SanitizeFileNameBase(newDisplayName);
        if (sanitizedBaseName.empty())
            return false;

        const std::filesystem::path oldPath = assetsDirectory / assetRelativePath;
        const std::string oldAssetKey = "Assets/" + assetRelativePath.generic_string();
        std::error_code errorCode;
        if (!std::filesystem::exists(oldPath, errorCode) || !std::filesystem::is_regular_file(oldPath, errorCode))
            return false;

        Assets::AssetType assetType = Assets::AssetType::Unknown;
        const auto oldRecordResult = Assets::AssetDatabase::GetInstance().FindByKey(oldAssetKey);
        if (oldRecordResult.IsSuccess())
            assetType = oldRecordResult.GetValue().Type;

        const std::string suffix = InferSuffixForAssetFileName(oldPath.filename().string());
        std::string finalFileName = sanitizedBaseName;
        if (!suffix.empty() && !EndsWithCaseInsensitive(finalFileName, suffix))
            finalFileName += suffix;

        const std::filesystem::path newPath = oldPath.parent_path() / finalFileName;
        if (newPath == oldPath)
        {
            if (outNewAssetRelativePath)
                *outNewAssetRelativePath = assetRelativePath;
            return true;
        }

        if (std::filesystem::exists(newPath, errorCode))
        {
            LT_CORE_WARN("RenameAssetInAssets: destination already exists: {}", newPath.string());
            return false;
        }

        std::filesystem::rename(oldPath, newPath, errorCode);
        if (errorCode)
            return false;

        const std::filesystem::path oldMetaPath = oldPath.parent_path() / (oldPath.filename().string() + ".meta");
        if (std::filesystem::exists(oldMetaPath, errorCode))
        {
            const std::filesystem::path newMetaPath = newPath.parent_path() / (newPath.filename().string() + ".meta");
            if (!std::filesystem::exists(newMetaPath, errorCode))
            {
                std::filesystem::rename(oldMetaPath, newMetaPath, errorCode);
            }
        }

        (void)Assets::AssetImportPipeline::ReimportChanged(true);
        if (outNewAssetRelativePath)
        {
            std::error_code relativeErrorCode;
            *outNewAssetRelativePath = std::filesystem::relative(newPath, assetsDirectory, relativeErrorCode);
        }

        std::error_code relativeErrorCode;
        const std::filesystem::path newAssetRelativePath = std::filesystem::relative(newPath, assetsDirectory, relativeErrorCode);
        if (!relativeErrorCode)
        {
            const std::string newAssetKey = "Assets/" + newAssetRelativePath.generic_string();
            (void)Assets::AssetDatabase::GetInstance().ImportOrUpdate(newAssetKey, assetType);
        }

        return true;
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

    bool ImportExternalPathsToFolder(const std::vector<std::filesystem::path>& sourcePaths,
                                     const std::filesystem::path& destinationFolderRelativePath)
    {
        if (sourcePaths.empty())
        {
            return false;
        }

        auto rootResult = Assets::FindProjectRootFromWorkingDirectory();
        if (rootResult.IsFailure())
        {
            return false;
        }

        const std::filesystem::path destinationDirectory = rootResult.GetValue() / "Assets" / destinationFolderRelativePath;
        std::error_code errorCode;
        std::filesystem::create_directories(destinationDirectory, errorCode);
        if (errorCode)
        {
            LT_CORE_WARN("ImportExternalPathsToFolder: failed creating destination '{}': {}", destinationDirectory.string(), errorCode.message());
            return false;
        }

        bool anyImported = false;
        for (const auto& sourcePath : sourcePaths)
        {
            if (sourcePath.empty() || !std::filesystem::exists(sourcePath, errorCode))
            {
                continue;
            }

            const std::filesystem::path uniqueDestination = GetUniquePathInDirectory(destinationDirectory, sourcePath.filename());

            if (std::filesystem::is_directory(sourcePath, errorCode))
            {
                std::filesystem::copy(sourcePath,
                                      uniqueDestination,
                                      std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
                                      errorCode);
                if (!errorCode)
                {
                    anyImported = true;
                }
                else
                {
                    LT_CORE_WARN("ImportExternalPathsToFolder: directory copy failed '{}' -> '{}': {}",
                                 sourcePath.string(), uniqueDestination.string(), errorCode.message());
                    errorCode.clear();
                }
            }
            else if (std::filesystem::is_regular_file(sourcePath, errorCode))
            {
                std::filesystem::copy_file(sourcePath,
                                           uniqueDestination,
                                           std::filesystem::copy_options::overwrite_existing,
                                           errorCode);
                if (!errorCode)
                {
                    anyImported = true;
                }
                else
                {
                    LT_CORE_WARN("ImportExternalPathsToFolder: file copy failed '{}' -> '{}': {}",
                                 sourcePath.string(), uniqueDestination.string(), errorCode.message());
                    errorCode.clear();
                }
            }
        }

        if (anyImported)
        {
            // Ensure imported assets get metadata/import records immediately.
            (void)Assets::AssetImportPipeline::ReimportChanged(true);
        }

        return anyImported;
    }
}
