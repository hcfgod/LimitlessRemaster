#pragma once

#include <array>
#include <cctype>
#include <filesystem>
#include <string>

namespace Limitless::EditorAssetNaming
{
    inline bool EndsWithCaseInsensitive(const std::string& value, const std::string& suffix)
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

    inline std::string GetAssetDisplayNameFromFileName(const std::string& fileName)
    {
        constexpr std::array<const char*, 4> kCompoundSuffixes = {
            ".scene.json",
            ".material.json",
            ".inputactions.json",
            ".prefab.json"
        };

        for (const char* suffix : kCompoundSuffixes)
        {
            const std::string suffixString = suffix;
            if (EndsWithCaseInsensitive(fileName, suffixString))
                return fileName.substr(0, fileName.size() - suffixString.size());
        }

        return std::filesystem::path(fileName).stem().string();
    }

    inline std::string GetAssetDisplayNameFromPath(const std::filesystem::path& path)
    {
        return GetAssetDisplayNameFromFileName(path.filename().string());
    }

    inline std::string GetAssetDisplayNameFromAssetKey(const std::string& assetKey)
    {
        return GetAssetDisplayNameFromPath(std::filesystem::path(assetKey));
    }
}
