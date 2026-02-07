#include "Assets/AssetUtils.h"

#include "Core/Debug/Log.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>

namespace Limitless::Assets
{
    static std::string GetMetaPath(const std::string& assetPath)
    {
        return assetPath + ".meta";
    }

    static std::string RandomHex(uint32_t byteCount)
    {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<uint32_t> dist(0, 255);

        std::ostringstream oss;
        for (uint32_t i = 0; i < byteCount; ++i)
        {
            oss << std::hex << std::setw(2) << std::setfill('0') << dist(gen);
        }
        return oss.str();
    }

    std::string GenerateGuid()
    {
        // 128-bit UUID-like (8-4-4-4-12).
        std::string s = RandomHex(16);
        s.insert(8, "-");
        s.insert(13, "-");
        s.insert(18, "-");
        s.insert(23, "-");

        // UUID v4 + variant bits (best-effort).
        s[14] = '4';
        const char variantTable[4] = {'8', '9', 'a', 'b'};
        if (s.size() > 19)
        {
            const char c = s[19];
            const int idx = (c >= '0' && c <= '9') ? (c - '0') : 0;
            s[19] = variantTable[idx & 0x3];
        }

        return s;
    }

    Result<std::string> LoadOrCreateGuid(const std::string& assetPath, const nlohmann::json& extraMeta)
    {
        if (assetPath.empty())
        {
            return Result<std::string>(ErrorCode::InvalidArgument, "Asset path is empty");
        }

        const std::string metaPath = GetMetaPath(assetPath);

        // Try load existing.
        if (std::filesystem::exists(metaPath))
        {
            try
            {
                std::ifstream in(metaPath, std::ios::in | std::ios::binary);
                if (in.is_open())
                {
                    nlohmann::json j;
                    in >> j;
                    if (j.contains("guid") && j["guid"].is_string())
                    {
                        return j["guid"].get<std::string>();
                    }
                }
            }
            catch (const std::exception& e)
            {
                LT_CORE_WARN("Assets::LoadOrCreateGuid: failed to read meta '{}': {}", metaPath, e.what());
            }
        }

        // Create new meta.
        std::string guid = GenerateGuid();
        nlohmann::json j = extraMeta;
        j["guid"] = guid;

        try
        {
            const std::filesystem::path metaFsPath(metaPath);
            if (metaFsPath.has_parent_path())
            {
                std::filesystem::create_directories(metaFsPath.parent_path());
            }

            std::ofstream out(metaPath, std::ios::out | std::ios::binary);
            if (!out.is_open())
            {
                return Result<std::string>(ErrorCode::FileAccessDenied, "Failed to create meta file: " + metaPath);
            }

            out << j.dump(4);
        }
        catch (const std::exception& e)
        {
            return Result<std::string>(ErrorCode::FileAccessDenied, std::string("Failed to write meta file: ") + e.what());
        }

        return guid;
    }

    Result<void> WriteDependencies(const std::string& assetPath, const std::vector<std::string>& dependencies)
    {
        if (assetPath.empty())
        {
            return Result<void>(ErrorCode::InvalidArgument, "Asset path is empty");
        }

        const std::string metaPath = GetMetaPath(assetPath);
        nlohmann::json j;

        // Best-effort read existing.
        try
        {
            if (std::filesystem::exists(metaPath))
            {
                std::ifstream in(metaPath, std::ios::in | std::ios::binary);
                if (in.is_open())
                {
                    in >> j;
                }
            }
        }
        catch (const std::exception& e)
        {
            LT_CORE_WARN("Assets::WriteDependencies: failed to read meta '{}': {}", metaPath, e.what());
        }

        j["deps"] = dependencies;

        try
        {
            const std::filesystem::path metaFsPath(metaPath);
            if (metaFsPath.has_parent_path())
            {
                std::filesystem::create_directories(metaFsPath.parent_path());
            }

            std::ofstream out(metaPath, std::ios::out | std::ios::binary);
            if (!out.is_open())
            {
                return Result<void>(ErrorCode::FileAccessDenied, "Failed to write meta file: " + metaPath);
            }

            out << j.dump(4);
        }
        catch (const std::exception& e)
        {
            return Result<void>(ErrorCode::FileAccessDenied, std::string("Failed to write meta file: ") + e.what());
        }

        return Result<void>();
    }
}

